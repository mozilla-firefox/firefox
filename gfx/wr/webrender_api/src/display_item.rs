/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use euclid::{SideOffsets2D, Angle};
use peek_poke::PeekPoke;
use std::ops::Not;
// local imports
use crate::{font, SnapshotImageKey};
use crate::{APZScrollGeneration, HasScrollLinkedEffect, PipelineId, PropertyBinding};
use crate::serde::{Serialize, Deserialize};
use crate::color::ColorF;
use crate::image::{ColorDepth, ImageKey};
use crate::units::*;
use std::hash::{Hash, Hasher};

// ******************************************************************
// * NOTE: some of these structs have an "IMPLICIT" comment.        *
// * This indicates that the BuiltDisplayList will have serialized  *
// * a list of values nearby that this item consumes. The traversal *
// * iterator should handle finding these. DebugDisplayItem should  *
// * make them explicit.                                            *
// ******************************************************************

/// A tag that can be used to identify items during hit testing. If the tag
/// is missing then the item doesn't take part in hit testing at all. This
/// is composed of two numbers. In Servo, the first is an identifier while the
/// second is used to select the cursor that should be used during mouse
/// movement. In Gecko, the first is a scrollframe identifier, while the second
/// is used to store various flags that APZ needs to properly process input
/// events.
pub type ItemTag = (u64, u16);

#[repr(C)]
#[derive(Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash, Deserialize, MallocSizeOf, Serialize, PeekPoke)]
pub struct PrimitiveFlags(u8);

bitflags! {
    impl PrimitiveFlags: u8 {
        /// The CSS backface-visibility property (yes, it can be really granular)
        const IS_BACKFACE_VISIBLE = 1 << 0;
        /// If set, this primitive represents a scroll bar container
        const IS_SCROLLBAR_CONTAINER = 1 << 1;
        /// This is used as a performance hint - this primitive may be promoted to a native
        /// compositor surface under certain (implementation specific) conditions. This
        /// is typically used for large videos, and canvas elements.
        const PREFER_COMPOSITOR_SURFACE = 1 << 2;
        /// If set, this primitive can be passed directly to the compositor via its
        /// ExternalImageId, and the compositor will use the native image directly.
        /// Used as a further extension on top of PREFER_COMPOSITOR_SURFACE.
        const SUPPORTS_EXTERNAL_COMPOSITOR_SURFACE = 1 << 3;
        /// This flags disables snapping and forces anti-aliasing even if the primitive is axis-aligned.
        const ANTIALISED = 1 << 4;
        /// If true, this primitive is used as a background for checkerboarding
        const CHECKERBOARD_BACKGROUND = 1 << 5;
    }
}

impl core::fmt::Debug for PrimitiveFlags {
    fn fmt(&self, f: &mut core::fmt::Formatter) -> core::fmt::Result {
        if self.is_empty() {
            write!(f, "{:#x}", Self::empty().bits())
        } else {
            bitflags::parser::to_writer(self, f)
        }
    }
}

impl Default for PrimitiveFlags {
    fn default() -> Self {
        PrimitiveFlags::IS_BACKFACE_VISIBLE
    }
}

/// A grouping of fields a lot of display items need, just to avoid
/// repeating these over and over in this file.
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct CommonItemProperties {
    /// Bounds of the display item to clip to. Many items are logically
    /// infinite, and rely on this clip_rect to define their bounds
    /// (solid colors, background-images, gradients, etc).
    pub clip_rect: LayoutRect,
    /// Additional clips
    pub clip_chain_id: ClipChainId,
    /// The coordinate-space the item is in (yes, it can be really granular)
    pub spatial_id: SpatialId,
    /// Various flags describing properties of this primitive.
    pub flags: PrimitiveFlags,
}

impl CommonItemProperties {
    /// Convenience for tests.
    pub fn new(
        clip_rect: LayoutRect,
        space_and_clip: SpaceAndClipInfo,
    ) -> Self {
        Self {
            clip_rect,
            spatial_id: space_and_clip.spatial_id,
            clip_chain_id: space_and_clip.clip_chain_id,
            flags: PrimitiveFlags::default(),
        }
    }
}

/// Per-primitive information about the nodes in the clip tree and
/// the spatial tree that the primitive belongs to.
///
/// Note: this is a separate struct from `PrimitiveInfo` because
/// it needs indirectional mapping during the DL flattening phase,
/// turning into `ScrollNodeAndClipChain`.
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct SpaceAndClipInfo {
    pub spatial_id: SpatialId,
    pub clip_chain_id: ClipChainId,
}

impl SpaceAndClipInfo {
    /// Create a new space/clip info associated with the root
    /// scroll frame.
    pub fn root_scroll(pipeline_id: PipelineId) -> Self {
        SpaceAndClipInfo {
            spatial_id: SpatialId::root_scroll_node(pipeline_id),
            clip_chain_id: ClipChainId::INVALID,
        }
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum SpatialTreeItem {
    ScrollFrame(ScrollFrameDescriptor),
    ReferenceFrame(ReferenceFrameDescriptor),
    StickyFrame(StickyFrameDescriptor),
    Invalid,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum DisplayItem {
    // These are the "real content" display items
    Rectangle(RectangleDisplayItem),
    HitTest(HitTestDisplayItem),
    Text(TextDisplayItem),
    Line(LineDisplayItem),
    Border(BorderDisplayItem),
    BoxShadow(BoxShadowDisplayItem),
    Gradient(GradientDisplayItem),
    RadialGradient(RadialGradientDisplayItem),
    ConicGradient(ConicGradientDisplayItem),
    Image(ImageDisplayItem),
    RepeatingImage(RepeatingImageDisplayItem),
    YuvImage(YuvImageDisplayItem),
    BackdropFilter(BackdropFilterDisplayItem),

    // Clips
    RectClip(RectClipDisplayItem),
    RoundedRectClip(RoundedRectClipDisplayItem),
    ImageMaskClip(ImageMaskClipDisplayItem),
    ClipChain(ClipChainItem),

    // Spaces and Frames that content can be scoped under.
    Iframe(IframeDisplayItem),
    PushReferenceFrame(ReferenceFrameDisplayListItem),
    PushStackingContext(PushStackingContextDisplayItem),

    // These marker items indicate an array of data follows, to be used for the
    // next non-marker item.
    SetGradientStops,
    SetFilterOps,
    SetFilterData,
    SetPoints,

    // These marker items terminate a scope introduced by a previous item.
    PopReferenceFrame,
    PopStackingContext,

    // For debugging purposes.
    DebugMarker(u32),
}

/// This is a "complete" version of the DisplayItem, with all implicit trailing
/// arrays included, for debug serialization (captures).
#[cfg(any(feature = "serialize", feature = "deserialize"))]
#[cfg_attr(feature = "serialize", derive(Serialize))]
#[cfg_attr(feature = "deserialize", derive(Deserialize))]
pub enum DebugDisplayItem {
    Rectangle(RectangleDisplayItem),
    HitTest(HitTestDisplayItem),
    Text(TextDisplayItem, Vec<font::GlyphInstance>),
    Line(LineDisplayItem),
    Border(BorderDisplayItem),
    BoxShadow(BoxShadowDisplayItem),
    Gradient(GradientDisplayItem),
    RadialGradient(RadialGradientDisplayItem),
    ConicGradient(ConicGradientDisplayItem),
    Image(ImageDisplayItem),
    RepeatingImage(RepeatingImageDisplayItem),
    YuvImage(YuvImageDisplayItem),
    BackdropFilter(BackdropFilterDisplayItem),

    ImageMaskClip(ImageMaskClipDisplayItem),
    RoundedRectClip(RoundedRectClipDisplayItem),
    RectClip(RectClipDisplayItem),
    ClipChain(ClipChainItem, Vec<ClipId>),

    Iframe(IframeDisplayItem),
    PushReferenceFrame(ReferenceFrameDisplayListItem),
    PushStackingContext(PushStackingContextDisplayItem),

    SetGradientStops(Vec<GradientStop>),
    SetFilterOps(Vec<FilterOp>),
    SetFilterData(FilterData),
    SetPoints(Vec<LayoutPoint>),

    PopReferenceFrame,
    PopStackingContext,

    DebugMarker(u32)
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ImageMaskClipDisplayItem {
    pub id: ClipId,
    pub spatial_id: SpatialId,
    pub image_mask: ImageMask,
    pub fill_rule: FillRule,
} // IMPLICIT points: Vec<LayoutPoint>

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct RectClipDisplayItem {
    pub id: ClipId,
    pub spatial_id: SpatialId,
    pub clip_rect: LayoutRect,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct RoundedRectClipDisplayItem {
    pub id: ClipId,
    pub spatial_id: SpatialId,
    pub clip: ComplexClipRegion,
    /// Layout-space outset applied when snapping this clip's rect. Non-zero
    /// only for the internal zero-blur box-shadow desugar, where the inner
    /// ClipOut edge must stay a constant distance from the snapped element
    /// (bug 2052033). All public callers leave this 0.
    pub snap_outset: f32,
}

/// The minimum and maximum allowable offset for a sticky frame in a single dimension.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct StickyOffsetBounds {
    /// The minimum offset for this frame, typically a negative value, which specifies how
    /// far in the negative direction the sticky frame can offset its contents in this
    /// dimension.
    pub min: f32,

    /// The maximum offset for this frame, typically a positive value, which specifies how
    /// far in the positive direction the sticky frame can offset its contents in this
    /// dimension.
    pub max: f32,
}

impl StickyOffsetBounds {
    pub fn new(min: f32, max: f32) -> StickyOffsetBounds {
        StickyOffsetBounds { min, max }
    }
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct StickyFrameDescriptor {
    pub id: SpatialId,
    pub parent_spatial_id: SpatialId,
    pub bounds: LayoutRect,

    /// The margins that should be maintained between the edge of the parent viewport and this
    /// sticky frame. A margin of None indicates that the sticky frame should not stick at all
    /// to that particular edge of the viewport.
    pub margins: SideOffsets2D<Option<f32>, LayoutPixel>,

    /// The minimum and maximum vertical offsets for this sticky frame. Ignoring these constraints,
    /// the sticky frame will continue to stick to the edge of the viewport as its original
    /// position is scrolled out of view. Constraints specify a maximum and minimum offset from the
    /// original position relative to non-sticky content within the same scrolling frame.
    pub vertical_offset_bounds: StickyOffsetBounds,

    /// The minimum and maximum horizontal offsets for this sticky frame. Ignoring these constraints,
    /// the sticky frame will continue to stick to the edge of the viewport as its original
    /// position is scrolled out of view. Constraints specify a maximum and minimum offset from the
    /// original position relative to non-sticky content within the same scrolling frame.
    pub horizontal_offset_bounds: StickyOffsetBounds,

    /// A property binding that we use to store an animation ID for APZ
    pub transform: Option<PropertyBinding<LayoutTransform>>,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ScrollFrameDescriptor {
    /// The id of the space this scroll frame creates
    pub scroll_frame_id: SpatialId,
    /// The size of the contents this contains (so the backend knows how far it can scroll).
    // FIXME: this can *probably* just be a size? Origin seems to just get thrown out.
    pub content_rect: LayoutRect,
    pub frame_rect: LayoutRect,
    pub parent_space: SpatialId,
    pub external_id: ExternalScrollId,
    /// The amount this scrollframe has already been scrolled by, in the caller.
    /// This means that all the display items that are inside the scrollframe
    /// will have their coordinates shifted by this amount, and this offset
    /// should be added to those display item coordinates in order to get a
    /// normalized value that is consistent across display lists.
    pub external_scroll_offset: LayoutVector2D,
    /// The generation of the external_scroll_offset.
    pub scroll_offset_generation: APZScrollGeneration,
    /// Whether this scrollframe document has any scroll-linked effect or not.
    pub has_scroll_linked_effect: HasScrollLinkedEffect,
}

/// A solid or an animating color to draw (may not actually be a rectangle due to complex clips)
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct RectangleDisplayItem {
    pub common: CommonItemProperties,
    pub bounds: LayoutRect,
    pub color: PropertyBinding<ColorF>,
}

/// A minimal hit-testable item for the parent browser's convenience, and is
/// slimmer than a RectangleDisplayItem (no color). The existence of this as a
/// distinct item also makes it easier to inspect/debug display items.
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct HitTestDisplayItem {
    pub rect: LayoutRect,
    pub clip_chain_id: ClipChainId,
    pub spatial_id: SpatialId,
    pub flags: PrimitiveFlags,
    pub tag: ItemTag,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct LineDisplayItem {
    pub common: CommonItemProperties,
    /// We need a separate rect from common.clip_rect to encode cute
    /// tricks that firefox does to make a series of text-decorations seamlessly
    /// line up -- snapping the decorations to a multiple of their period, and
    /// then clipping them to their "proper" area. This rect is that "logical"
    /// snapped area that may be clipped to the right size by the clip_rect.
    pub area: LayoutRect,
    /// Whether the rect is interpretted as vertical or horizontal
    pub orientation: LineOrientation,
    /// This could potentially be implied from area, but we currently prefer
    /// that this is the responsibility of the layout engine. Value irrelevant
    /// for non-wavy lines.
    // FIXME: this was done before we could use tagged unions in enums, but now
    // it should just be part of LineStyle::Wavy.
    pub wavy_line_thickness: f32,
    pub color: ColorF,
    pub style: LineStyle,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize, Eq, Hash, PeekPoke)]
pub enum LineOrientation {
    Vertical,
    Horizontal,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize, Eq, Hash, PeekPoke)]
pub enum LineStyle {
    Solid,
    Dotted,
    Dashed,
    Wavy,
}

/// Identifies whether a text run is a normal (drawable) run or a shadow copy
/// produced by desugaring a text-shadow, and if the latter whether the shadow
/// is blurred. The scene builder maps this onto the two properties the old
/// scene-builder shadow expansion baked into the shadow's `TextRun` key: the
/// `shadow` flag (which makes color-bitmap glyphs sample alpha only), and the
/// requested raster space (a blurred shadow rasterizes in `Local(1.0)` space,
/// like the removed `TextRun::create_shadow`).
#[repr(u8)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, Eq, Hash, PeekPoke)]
pub enum GlyphShadowMode {
    /// Not a shadow: normal glyphs, raster space taken from the stack.
    #[default]
    None,
    /// Shadow with no (noop) blur: shadow glyphs, raster space from the stack.
    Unblurred,
    /// Blurred shadow: shadow glyphs, raster space forced to `Local(1.0)`.
    Blurred,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct TextDisplayItem {
    pub common: CommonItemProperties,
    /// The area all the glyphs should be found in. Strictly speaking this isn't
    /// necessarily needed, but layout engines should already "know" this, and we
    /// use it cull and size things quickly before glyph layout is done. Currently
    /// the glyphs *can* be outside these bounds, but that should imply they
    /// can be cut off.
    // FIXME: these are currently sometimes ignored to keep some old wrench tests
    // working, but we should really just fix the tests!
    pub bounds: LayoutRect,
    pub font_key: font::FontInstanceKey,
    pub color: ColorF,
    pub glyph_options: Option<font::GlyphOptions>,
    /// Set by the display-list builder's shadow desugaring on the offset copies
    /// of shadowed text. `None` for ordinary text.
    pub shadow: GlyphShadowMode,
} // IMPLICIT: glyphs: Vec<font::GlyphInstance>

#[derive(Clone, Copy, Debug, Default, Deserialize, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct NormalBorder {
    pub left: BorderSide,
    pub right: BorderSide,
    pub top: BorderSide,
    pub bottom: BorderSide,
    pub radius: BorderRadius,

    /// Accumulated inset from the reference shape, in case of inflation/deflation.
    /// The inset is expected to be already baked in rect and radii, but is necessary
    /// for the correct computation of corner shapes.
    ///
    /// As borders have widths, this value should be calculated for the *outer* edge
    /// of the border. For instance, a regular shape with a border will have an inset
    /// of zero, but an outline of 5px, of which the outer edge is 5px away from the
    /// original border-box, will have an inset of -5px.
    pub inset: LayoutSideOffsets,

    /// Whether to apply anti-aliasing on the border corners.
    ///
    /// Note that for this to be `false` and work, this requires the borders to
    /// be solid, and no border-radius.
    pub do_aa: bool,
}

impl NormalBorder {
    /// Return a copy of this border with every side's color replaced by
    /// `color`, keeping styles, radius and anti-aliasing. Used to recolor a
    /// border into its shadow.
    pub fn with_color(&self, color: ColorF) -> Self {
        let mut b = *self;
        b.left.color = color;
        b.right.color = color;
        b.top.color = color;
        b.bottom.color = color;
        b
    }

    fn can_disable_antialiasing(&self) -> bool {
        fn is_valid(style: BorderStyle) -> bool {
            style == BorderStyle::Solid || style == BorderStyle::None
        }

        self.radius.is_zero() &&
            is_valid(self.top.style) &&
            is_valid(self.left.style) &&
            is_valid(self.bottom.style) &&
            is_valid(self.right.style)
    }

    /// Normalizes a border so that we don't render disallowed stuff, like inset
    /// borders that are less than two pixels wide.
    #[inline]
    pub fn normalize(&mut self, widths: &LayoutSideOffsets) {
        debug_assert!(
            self.do_aa || self.can_disable_antialiasing(),
            "Unexpected disabled-antialiasing in a border, likely won't work or will be ignored"
        );

        #[inline]
        fn renders_small_border_solid(style: BorderStyle) -> bool {
            match style {
                BorderStyle::Groove |
                BorderStyle::Ridge => true,
                _ => false,
            }
        }

        let normalize_side = |side: &mut BorderSide, width: f32| {
            if renders_small_border_solid(side.style) && width < 2. {
                side.style = BorderStyle::Solid;
            }
        };

        normalize_side(&mut self.left, widths.left);
        normalize_side(&mut self.right, widths.right);
        normalize_side(&mut self.top, widths.top);
        normalize_side(&mut self.bottom, widths.bottom);
    }
}

#[repr(u8)]
#[derive(Debug, Copy, Clone, MallocSizeOf, PartialEq, Serialize, Deserialize, Eq, Hash, PeekPoke)]
pub enum RepeatMode {
    Stretch,
    Repeat,
    Round,
    Space,
}

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum NinePatchBorderSource {
    Image(ImageKey, ImageRendering),
    Gradient(Gradient),
    RadialGradient(RadialGradient),
    ConicGradient(ConicGradient),
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct NinePatchBorder {
    /// Describes what to use as the 9-patch source image. If this is an image,
    /// it will be stretched to fill the size given by width x height.
    pub source: NinePatchBorderSource,

    /// The width of the 9-part image.
    pub width: i32,

    /// The height of the 9-part image.
    pub height: i32,

    /// Distances from each edge where the image should be sliced up. These
    /// values are in 9-part-image space (the same space as width and height),
    /// and the resulting image parts will be used to fill the corresponding
    /// parts of the border as given by the border widths. This can lead to
    /// stretching.
    /// Slices can be overlapping. In that case, the same pixels from the
    /// 9-part image will show up in multiple parts of the resulting border.
    pub slice: DeviceIntSideOffsets,

    /// Controls whether the center of the 9 patch image is rendered or
    /// ignored. The center is never rendered if the slices are overlapping.
    pub fill: bool,

    /// Determines what happens if the horizontal side parts of the 9-part
    /// image have a different size than the horizontal parts of the border.
    pub repeat_horizontal: RepeatMode,

    /// Determines what happens if the vertical side parts of the 9-part
    /// image have a different size than the vertical parts of the border.
    pub repeat_vertical: RepeatMode,
}

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum BorderDetails {
    Normal(NormalBorder),
    NinePatch(NinePatchBorder),
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct BorderDisplayItem {
    pub common: CommonItemProperties,
    pub bounds: LayoutRect,
    pub widths: LayoutSideOffsets,
    pub details: BorderDetails,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum BorderRadiusKind {
    Uniform,
    NonUniform,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct BorderRadius {
    pub top_left: LayoutSize,
    pub top_right: LayoutSize,
    pub bottom_left: LayoutSize,
    pub bottom_right: LayoutSize,
    pub shape_top_left: f32,
    pub shape_top_right: f32,
    pub shape_bottom_left: f32,
    pub shape_bottom_right: f32,
}

impl Default for BorderRadius {
    fn default() -> Self {
        BorderRadius {
            top_left: LayoutSize::zero(),
            top_right: LayoutSize::zero(),
            bottom_left: LayoutSize::zero(),
            bottom_right: LayoutSize::zero(),
            shape_top_left: 1.0,
            shape_top_right: 1.0,
            shape_bottom_left: 1.0,
            shape_bottom_right: 1.0,
        }
    }
}

impl BorderRadius {
    /// True when every corner uses the default round (ellipse) shape.
    pub fn shapes_all_round(&self) -> bool {
        self.shape_top_left == 1.0 &&
        self.shape_top_right == 1.0 &&
        self.shape_bottom_left == 1.0 &&
        self.shape_bottom_right == 1.0
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct BorderSide {
    pub color: ColorF,
    pub style: BorderStyle,
}

#[repr(u32)]
#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize, Hash, Eq, PeekPoke)]
pub enum BorderStyle {
    None = 0,
    Solid = 1,
    Double = 2,
    Dotted = 3,
    Dashed = 4,
    Hidden = 5,
    Groove = 6,
    Ridge = 7,
    Inset = 8,
    Outset = 9,
}

impl BorderStyle {
    pub fn is_hidden(self) -> bool {
        self == BorderStyle::Hidden || self == BorderStyle::None
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum BoxShadowClipMode {
    Outset = 0,
    Inset = 1,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct BoxShadowDisplayItem {
    pub common: CommonItemProperties,
    pub box_bounds: LayoutRect,
    pub offset: LayoutVector2D,
    pub color: ColorF,
    pub blur_radius: f32,
    pub spread_radius: f32,
    pub border_radius: BorderRadius,
    pub shadow_radius: BorderRadius,
    pub clip_mode: BoxShadowClipMode,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct Shadow {
    pub offset: LayoutVector2D,
    pub color: ColorF,
    pub blur_radius: f32,
}

#[repr(u8)]
#[derive(Debug, Copy, Clone, Hash, Eq, MallocSizeOf, PartialEq, Serialize, Deserialize, Ord, PartialOrd, PeekPoke)]
pub enum ExtendMode {
    Clamp,
    Repeat,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct Gradient {
    pub start_point: LayoutPoint,
    pub end_point: LayoutPoint,
    pub extend_mode: ExtendMode,
} // IMPLICIT: stops: Vec<GradientStop>

impl Gradient {
    pub fn is_valid(&self) -> bool {
        self.start_point.x.is_finite() &&
            self.start_point.y.is_finite() &&
            self.end_point.x.is_finite() &&
            self.end_point.y.is_finite()
    }
}

/// The area
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct GradientDisplayItem {
    /// NOTE: common.clip_rect is the area the gradient covers
    pub common: CommonItemProperties,
    /// The area to tile the gradient over (first tile starts at origin of this rect)
    // FIXME: this should ideally just be `tile_origin` here, with the clip_rect
    // defining the bounds of the item. Needs non-trivial backend changes.
    pub bounds: LayoutRect,
    /// How big a tile of the of the gradient should be (common case: bounds.size)
    pub tile_size: LayoutSize,
    /// The space between tiles of the gradient (common case: 0)
    pub tile_spacing: LayoutSize,
    pub gradient: Gradient,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct GradientStop {
    pub offset: f32,
    pub color: ColorF,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct RadialGradient {
    pub center: LayoutPoint,
    pub radius: LayoutSize,
    pub start_offset: f32,
    pub end_offset: f32,
    pub extend_mode: ExtendMode,
} // IMPLICIT stops: Vec<GradientStop>

impl RadialGradient {
    pub fn is_valid(&self) -> bool {
        self.center.x.is_finite() &&
            self.center.y.is_finite() &&
            self.start_offset.is_finite() &&
            self.end_offset.is_finite()
    }
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ConicGradient {
    pub center: LayoutPoint,
    pub angle: f32,
    pub start_offset: f32,
    pub end_offset: f32,
    pub extend_mode: ExtendMode,
} // IMPLICIT stops: Vec<GradientStop>

impl ConicGradient {
    pub fn is_valid(&self) -> bool {
        self.center.x.is_finite() &&
            self.center.y.is_finite() &&
            self.angle.is_finite() &&
            self.start_offset.is_finite() &&
            self.end_offset.is_finite()
    }
}

/// Just an abstraction for bundling up a bunch of clips into a "super clip".
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ClipChainItem {
    pub id: ClipChainId,
    pub parent: Option<ClipChainId>,
} // IMPLICIT clip_ids: Vec<ClipId>

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct RadialGradientDisplayItem {
    pub common: CommonItemProperties,
    /// The area to tile the gradient over (first tile starts at origin of this rect)
    // FIXME: this should ideally just be `tile_origin` here, with the clip_rect
    // defining the bounds of the item. Needs non-trivial backend changes.
    pub bounds: LayoutRect,
    pub gradient: RadialGradient,
    pub tile_size: LayoutSize,
    pub tile_spacing: LayoutSize,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ConicGradientDisplayItem {
    pub common: CommonItemProperties,
    /// The area to tile the gradient over (first tile starts at origin of this rect)
    // FIXME: this should ideally just be `tile_origin` here, with the clip_rect
    // defining the bounds of the item. Needs non-trivial backend changes.
    pub bounds: LayoutRect,
    pub gradient: ConicGradient,
    pub tile_size: LayoutSize,
    pub tile_spacing: LayoutSize,
}

/// Renders a filtered region of its backdrop
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct BackdropFilterDisplayItem {
    pub common: CommonItemProperties,
}
// IMPLICIT: filters: Vec<FilterOp>, filter_datas: Vec<FilterData>, filter_primitives: Vec<FilterPrimitive>

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ReferenceFrameDisplayListItem {
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ReferenceFrameDescriptor {
    pub origin: LayoutPoint,
    pub parent_spatial_id: SpatialId,
    pub reference_frame: ReferenceFrame,
}

#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum ReferenceFrameKind {
    /// A normal transform matrix, may contain perspective (the CSS transform property)
    Transform {
        /// Optionally marks the transform as only ever having a simple 2D scale or translation,
        /// allowing for optimizations.
        is_2d_scale_translation: bool,
        /// Marks that the transform should be snapped. Used for transforms which animate in
        /// response to scrolling, eg for zooming or dynamic toolbar fixed-positioning.
        should_snap: bool,
        /// Marks the transform being a part of the CSS stacking context that also has
        /// a perspective. In this case, backface visibility takes this perspective into
        /// account.
        paired_with_perspective: bool,
    },
    /// A perspective transform, that optionally scrolls relative to a specific scroll node
    Perspective {
        scrolling_relative_to: Option<ExternalScrollId>,
    }
}

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum Rotation {
    Degree0,
    Degree90,
    Degree180,
    Degree270,
}

impl Rotation {
    pub fn to_matrix(
        &self,
        size: LayoutSize,
    ) -> LayoutTransform {
        let (shift_center_to_origin, angle) = match self {
            Rotation::Degree0 => {
              (LayoutTransform::translation(-size.width / 2., -size.height / 2., 0.), Angle::degrees(0.))
            },
            Rotation::Degree90 => {
              (LayoutTransform::translation(-size.height / 2., -size.width / 2., 0.), Angle::degrees(90.))
            },
            Rotation::Degree180 => {
              (LayoutTransform::translation(-size.width / 2., -size.height / 2., 0.), Angle::degrees(180.))
            },
            Rotation::Degree270 => {
              (LayoutTransform::translation(-size.height / 2., -size.width / 2., 0.), Angle::degrees(270.))
            },
        };
        let shift_origin_to_center = LayoutTransform::translation(size.width / 2., size.height / 2., 0.);

        shift_center_to_origin
            .then(&LayoutTransform::rotation(0., 0., 1.0, angle))
            .then(&shift_origin_to_center)
    }
}

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum ReferenceTransformBinding {
    /// Standard reference frame which contains a precomputed transform.
    Static {
        binding: PropertyBinding<LayoutTransform>,
    },
    /// Computed reference frame which dynamically calculates the transform
    /// based on the given parameters. The reference is the content size of
    /// the parent iframe, which is affected by snapping.
    ///
    /// This is used when a transform depends on the layout size of an
    /// element, otherwise the difference between the unsnapped size
    /// used in the transform, and the snapped size calculated during scene
    /// building can cause seaming.
    Computed {
        scale_from: Option<LayoutSize>,
        vertical_flip: bool,
        rotation: Rotation,
    },
}

impl Default for ReferenceTransformBinding {
    fn default() -> Self {
        ReferenceTransformBinding::Static {
            binding: Default::default(),
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ReferenceFrame {
    pub kind: ReferenceFrameKind,
    pub transform_style: TransformStyle,
    /// The transform matrix, either the perspective matrix or the transform
    /// matrix.
    pub transform: ReferenceTransformBinding,
    pub id: SpatialId,
}

/// If passed in a stacking context display item, inform WebRender that
/// the contents of the stacking context should be retained into a texture
/// and associated to an image key.
///
/// Image display items can then display the cached snapshot using the
/// same image key.
///
/// The flow for creating/using/deleting snapshots is the same as with
/// regular images:
///  - The image key must have been created with `Transaction::add_snapshot_image`.
///  - The current scene must not contain references to the snapshot when
///    `Transaction::delete_snapshot_image` is called.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct SnapshotInfo {
    /// The image key to associate the snapshot with.
    pub key: SnapshotImageKey,
    /// The bounds of the snapshot in local space.
    ///
    /// This rectangle is relative to the same coordinate space as the
    /// child items of the stacking context.
    pub area: LayoutRect,
    /// If true, detach the stacking context from the scene and only
    /// render it into the snapshot.
    /// If false, the stacking context rendered in the frame normally
    /// in addition to being cached into the snapshot.
    pub detached: bool,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct PushStackingContextDisplayItem {
    pub spatial_id: SpatialId,
    pub snapshot: Option<SnapshotInfo>,
    pub prim_flags: PrimitiveFlags,
    pub stacking_context: StackingContext,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct StackingContext {
    pub transform_style: TransformStyle,
    pub mix_blend_mode: MixBlendMode,
    pub clip_chain_id: Option<ClipChainId>,
    /// Raster space already resolved against the enclosing stacking contexts by
    /// `DisplayListBuilder::push_stacking_context`, not the space its caller
    /// requested. Resolving on the builder means one raster space stack rather
    /// than two that have to agree.
    pub raster_space: RasterSpace,
    pub flags: StackingContextFlags,
}
// IMPLICIT: filters: Vec<FilterOp>, filter_datas: Vec<FilterData>, filter_primitives: Vec<FilterPrimitive>

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum TransformStyle {
    Flat = 0,
    Preserve3D = 1,
}

/// Configure whether the contents of a stacking context
/// should be rasterized in local space or screen space.
/// Local space rasterized pictures are typically used
/// when we want to cache the output, and performance is
/// important. Note that this is a performance hint only,
/// which WR may choose to ignore.
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, MallocSizeOf, Serialize, PeekPoke)]
#[repr(u8)]
pub enum RasterSpace {
    // Rasterize in local-space, applying supplied scale to primitives.
    // Best performance, but lower quality.
    Local(f32),

    // Rasterize the picture in screen-space, including rotation / skew etc in
    // the rasterized element. Best quality, but slower performance. Note that
    // any stacking context with a perspective transform will be rasterized
    // in local-space, even if this is set.
    Screen,
}

impl RasterSpace {
    pub fn local_scale(self) -> Option<f32> {
        match self {
            RasterSpace::Local(scale) => Some(scale),
            RasterSpace::Screen => None,
        }
    }
}

impl Eq for RasterSpace {}

impl Hash for RasterSpace {
    fn hash<H: Hasher>(&self, state: &mut H) {
        match self {
            RasterSpace::Screen => {
                0.hash(state);
            }
            RasterSpace::Local(scale) => {
                // Note: this is inconsistent with the Eq impl for -0.0 (don't care).
                1.hash(state);
                scale.to_bits().hash(state);
            }
        }
    }
}

#[repr(C)]
#[derive(Copy, PartialEq, Eq, Clone, PartialOrd, Ord, Hash, Deserialize, MallocSizeOf, Serialize, PeekPoke)]
pub struct StackingContextFlags(u8);

bitflags! {
    impl StackingContextFlags: u8 {
        /// If true, this stacking context is a blend container than contains
        /// mix-blend-mode children (and should thus be isolated).
        const IS_BLEND_CONTAINER = 1 << 0;
        /// If true, this stacking context is a wrapper around a backdrop-filter (e.g. for
        /// a clip-mask). This is needed to allow the correct selection of a backdrop root
        /// since a clip-mask stacking context creates a parent surface.
        const WRAPS_BACKDROP_FILTER = 1 << 1;
        /// If true, this stacking context must be isolated from parent by a surface.
        const FORCED_ISOLATION = 1 << 2;
    }
}

impl core::fmt::Debug for StackingContextFlags {
    fn fmt(&self, f: &mut core::fmt::Formatter) -> core::fmt::Result {
        if self.is_empty() {
            write!(f, "{:#x}", Self::empty().bits())
        } else {
            bitflags::parser::to_writer(self, f)
        }
    }
}

impl Default for StackingContextFlags {
    fn default() -> Self {
        StackingContextFlags::empty()
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum MixBlendMode {
    Normal = 0,
    Multiply = 1,
    Screen = 2,
    Overlay = 3,
    Darken = 4,
    Lighten = 5,
    ColorDodge = 6,
    ColorBurn = 7,
    HardLight = 8,
    SoftLight = 9,
    Difference = 10,
    Exclusion = 11,
    Hue = 12,
    Saturation = 13,
    Color = 14,
    Luminosity = 15,
    PlusLighter = 16,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum ColorSpace {
    Srgb,
    LinearRgb,
}

/// Available composite operoations for the composite filter primitive
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum CompositeOperator {
    Over,
    In,
    Atop,
    Out,
    Xor,
    Lighter,
    Arithmetic([f32; 4]),
}

impl CompositeOperator {
    // This must stay in sync with the composite operator defines in cs_svg_filter.glsl
    pub fn as_int(&self) -> u32 {
        match self {
            CompositeOperator::Over => 0,
            CompositeOperator::In => 1,
            CompositeOperator::Out => 2,
            CompositeOperator::Atop => 3,
            CompositeOperator::Xor => 4,
            CompositeOperator::Lighter => 5,
            CompositeOperator::Arithmetic(..) => 6,
        }
    }
}

/// An input to a SVG filter primitive.
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum FilterPrimitiveInput {
    /// The input is the original graphic that the filter is being applied to.
    Original,
    /// The input is the output of the previous filter primitive in the filter primitive chain.
    Previous,
    /// The input is the output of the filter primitive at the given index in the filter primitive chain.
    OutputOfPrimitiveIndex(usize),
}

impl FilterPrimitiveInput {
    /// Gets the index of the input.
    /// Returns `None` if the source graphic is the input.
    pub fn to_index(self, cur_index: usize) -> Option<usize> {
        match self {
            FilterPrimitiveInput::Previous if cur_index > 0 => Some(cur_index - 1),
            FilterPrimitiveInput::OutputOfPrimitiveIndex(index) => Some(index),
            _ => None,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct BlendPrimitive {
    pub input1: FilterPrimitiveInput,
    pub input2: FilterPrimitiveInput,
    pub mode: MixBlendMode,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct FloodPrimitive {
    pub color: ColorF,
}

impl FloodPrimitive {
    pub fn sanitize(&mut self) {
        self.color.r = self.color.r.clamp(0.0, 1.0);
        self.color.g = self.color.g.clamp(0.0, 1.0);
        self.color.b = self.color.b.clamp(0.0, 1.0);
        self.color.a = self.color.a.clamp(0.0, 1.0);
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct BlurPrimitive {
    pub input: FilterPrimitiveInput,
    pub width: f32,
    pub height: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct OpacityPrimitive {
    pub input: FilterPrimitiveInput,
    pub opacity: f32,
}

impl OpacityPrimitive {
    pub fn sanitize(&mut self) {
        self.opacity = self.opacity.clamp(0.0, 1.0);
    }
}

/// cbindgen:derive-eq=false
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ColorMatrixPrimitive {
    pub input: FilterPrimitiveInput,
    pub matrix: [f32; 20],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct DropShadowPrimitive {
    pub input: FilterPrimitiveInput,
    pub shadow: Shadow,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ComponentTransferPrimitive {
    pub input: FilterPrimitiveInput,
    // Component transfer data is stored in FilterData.
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct IdentityPrimitive {
    pub input: FilterPrimitiveInput,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct OffsetPrimitive {
    pub input: FilterPrimitiveInput,
    pub offset: LayoutVector2D,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct CompositePrimitive {
    pub input1: FilterPrimitiveInput,
    pub input2: FilterPrimitiveInput,
    pub operator: CompositeOperator,
}

/// See: https://github.com/eqrion/cbindgen/issues/9
/// cbindgen:derive-eq=false
#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize, PeekPoke)]
pub enum FilterPrimitiveKind {
    Identity(IdentityPrimitive),
    Blend(BlendPrimitive),
    Flood(FloodPrimitive),
    Blur(BlurPrimitive),
    // TODO: Support animated opacity?
    Opacity(OpacityPrimitive),
    /// cbindgen:derive-eq=false
    ColorMatrix(ColorMatrixPrimitive),
    DropShadow(DropShadowPrimitive),
    ComponentTransfer(ComponentTransferPrimitive),
    Offset(OffsetPrimitive),
    Composite(CompositePrimitive),
}

impl Default for FilterPrimitiveKind {
    fn default() -> Self {
        FilterPrimitiveKind::Identity(IdentityPrimitive::default())
    }
}

impl FilterPrimitiveKind {
    pub fn sanitize(&mut self) {
        match self {
            FilterPrimitiveKind::Flood(flood) => flood.sanitize(),
            FilterPrimitiveKind::Opacity(opacity) => opacity.sanitize(),

            // No sanitization needed.
            FilterPrimitiveKind::Identity(..) |
            FilterPrimitiveKind::Blend(..) |
            FilterPrimitiveKind::ColorMatrix(..) |
            FilterPrimitiveKind::Offset(..) |
            FilterPrimitiveKind::Composite(..) |
            FilterPrimitiveKind::Blur(..) |
            FilterPrimitiveKind::DropShadow(..) |
            // Component transfer's filter data is sanitized separately.
            FilterPrimitiveKind::ComponentTransfer(..) => {}
        }
    }
}

/// SVG Filter Primitive.
/// See: https://github.com/eqrion/cbindgen/issues/9
/// cbindgen:derive-eq=false
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct FilterPrimitive {
    pub kind: FilterPrimitiveKind,
    pub color_space: ColorSpace,
}

impl FilterPrimitive {
    pub fn sanitize(&mut self) {
        self.kind.sanitize();
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, Eq, PartialEq, Serialize, PeekPoke)]
pub enum FilterOpGraphPictureBufferId {
    #[default]
    /// empty slot in feMerge inputs
    None,
    /// reference to another (earlier) node in filter graph
    BufferId(i16),
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, Serialize, PeekPoke)]
pub struct FilterOpGraphPictureReference {
    /// Id of the picture in question in a namespace unique to this filter DAG
    pub buffer_id: FilterOpGraphPictureBufferId,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, Serialize, PeekPoke)]
pub struct FilterOpGraphNode {
    /// True if color_interpolation_filter == LinearRgb; shader will convert
    /// sRGB texture pixel colors on load and convert back on store, for correct
    /// interpolation
    pub linear: bool,
    /// virtualized picture input binding 1 (i.e. texture source), typically
    /// this is used, but certain filters do not use it
    pub input: FilterOpGraphPictureReference,
    /// virtualized picture input binding 2 (i.e. texture sources), only certain
    /// filters use this
    pub input2: FilterOpGraphPictureReference,
    /// rect this node will render into, in filter space
    pub subregion: LayoutRect,
}

/// Maximum number of SVGFE filters in one graph, this is constant size to avoid
/// allocating anything, and the SVG spec allows us to drop all filters on an
/// item if the graph is excessively complex - a graph this large will never be
/// a good user experience, performance-wise.
pub const SVGFE_GRAPH_MAX: usize = 256;

#[repr(C)]
#[derive(Clone, Copy, Debug, Deserialize, Serialize, PeekPoke)]
pub enum FilterOp {
    /// Filter that does no transformation of the colors, needed for
    /// debug purposes, and is the default value in impl_default_for_enums.
    /// parameters: none
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Identity,
    /// apply blur effect
    /// parameters: stdDeviationX, stdDeviationY, should_inflate
    /// `should_inflate` controls whether WebRender inflates the blur surface by
    /// the sample radius; callers that have already inflated the bounds (e.g.
    /// text-shadow) pass false. CSS/SVG filter blurs pass true.
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Blur(f32, f32, bool),
    /// apply brightness effect
    /// parameters: amount
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Brightness(f32),
    /// apply contrast effect
    /// parameters: amount
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Contrast(f32),
    /// fade image toward greyscale version of image
    /// parameters: amount
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Grayscale(f32),
    /// fade image toward hue-rotated version of image (rotate RGB around color wheel)
    /// parameters: angle
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    HueRotate(f32),
    /// fade image toward inverted image (1 - RGB)
    /// parameters: amount
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Invert(f32),
    /// multiplies color and alpha by opacity
    /// parameters: amount
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Opacity(PropertyBinding<f32>, f32),
    /// multiply saturation of colors
    /// parameters: amount
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Saturate(f32),
    /// fade image toward sepia tone version of image
    /// parameters: amount
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    Sepia(f32),
    /// add drop shadow version of image to the image
    /// parameters: shadow
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    DropShadow(Shadow),
    /// transform color and alpha in image through 4x5 color matrix (transposed for efficiency)
    /// parameters: matrix[5][4]
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    ColorMatrix([f32; 20]),
    /// internal use - convert sRGB input to linear output
    /// parameters: none
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    SrgbToLinear,
    /// internal use - convert linear input to sRGB output
    /// parameters: none
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    LinearToSrgb,
    /// remap RGBA with color gradients and component swizzle
    /// parameters: FilterData
    /// CSS filter semantics - operates on previous picture, uses sRGB space (non-linear)
    ComponentTransfer,
    /// replace image with a solid color
    /// NOTE: UNUSED; Gecko never produces this filter
    /// parameters: color
    /// CSS filter semantics - operates on previous picture,uses sRGB space (non-linear)
    Flood(ColorF),
    /// Filter that copies the SourceGraphic image into the specified subregion,
    /// This is intentionally the only way to get SourceGraphic into the graph,
    /// as the filter region must be applied before it is used.
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - no inputs, no linear
    SVGFESourceGraphic { node: FilterOpGraphNode },
    /// Filter that copies the SourceAlpha image into the specified subregion,
    /// This is intentionally the only way to get SourceGraphic into the graph,
    /// as the filter region must be applied before it is used.
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - no inputs, no linear
    SVGFESourceAlpha { node: FilterOpGraphNode },
    /// Filter that does no transformation of the colors, used for subregion
    /// cropping only.
    SVGFEIdentity { node: FilterOpGraphNode },
    /// represents CSS opacity property as a graph node like the rest of the SVGFE* filters
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    SVGFEOpacity { node: FilterOpGraphNode, valuebinding: PropertyBinding<f32>, value: f32 },
    /// convert a color image to an alpha channel - internal use; generated by
    /// SVGFilterInstance::GetOrCreateSourceAlphaIndex().
    SVGFEToAlpha { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_DARKEN
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feBlendElement
    SVGFEBlendDarken { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_LIGHTEN
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feBlendElement
    SVGFEBlendLighten { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_MULTIPLY
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feBlendElement
    SVGFEBlendMultiply { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_NORMAL
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feBlendElement
    SVGFEBlendNormal { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_SCREEN
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feBlendElement
    SVGFEBlendScreen { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_OVERLAY
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendOverlay { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_COLOR_DODGE
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendColorDodge { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_COLOR_BURN
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendColorBurn { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_HARD_LIGHT
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendHardLight { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_SOFT_LIGHT
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendSoftLight { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_DIFFERENCE
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendDifference { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_EXCLUSION
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendExclusion { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_HUE
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendHue { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_SATURATION
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendSaturation { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_COLOR
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendColor { node: FilterOpGraphNode },
    /// combine 2 images with SVG_FEBLEND_MODE_LUMINOSITY
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Source: https://developer.mozilla.org/en-US/docs/Web/CSS/mix-blend-mode
    SVGFEBlendLuminosity { node: FilterOpGraphNode },
    /// transform colors of image through 5x4 color matrix (transposed for efficiency)
    /// parameters: FilterOpGraphNode, matrix[5][4]
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feColorMatrixElement
    SVGFEColorMatrix { node: FilterOpGraphNode, values: [f32; 20] },
    /// transform colors of image through configurable gradients with component swizzle
    /// parameters: FilterOpGraphNode, FilterData
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feComponentTransferElement
    SVGFEComponentTransfer { node: FilterOpGraphNode },
    /// composite 2 images with chosen composite mode with parameters for that mode
    /// parameters: FilterOpGraphNode, k1, k2, k3, k4
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feCompositeElement
    SVGFECompositeArithmetic { node: FilterOpGraphNode, k1: f32, k2: f32, k3: f32,
        k4: f32 },
    /// composite 2 images with chosen composite mode with parameters for that mode
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feCompositeElement
    SVGFECompositeATop { node: FilterOpGraphNode },
    /// composite 2 images with chosen composite mode with parameters for that mode
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feCompositeElement
    SVGFECompositeIn { node: FilterOpGraphNode },
    /// composite 2 images with chosen composite mode with parameters for that mode
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Docs: https://developer.mozilla.org/en-US/docs/Web/SVG/Element/feComposite
    SVGFECompositeLighter { node: FilterOpGraphNode },
    /// composite 2 images with chosen composite mode with parameters for that mode
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feCompositeElement
    SVGFECompositeOut { node: FilterOpGraphNode },
    /// composite 2 images with chosen composite mode with parameters for that mode
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feCompositeElement
    SVGFECompositeOver { node: FilterOpGraphNode },
    /// composite 2 images with chosen composite mode with parameters for that mode
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feCompositeElement
    SVGFECompositeXOR { node: FilterOpGraphNode },
    /// transform image through convolution matrix of up to 25 values (spec
    /// allows more but for performance reasons we do not)
    /// parameters: FilterOpGraphNode, orderX, orderY, kernelValues[25],
    ///  divisor, bias, targetX, targetY, kernelUnitLengthX, kernelUnitLengthY,
    ///  preserveAlpha
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feConvolveMatrixElement
    SVGFEConvolveMatrixEdgeModeDuplicate { node: FilterOpGraphNode, order_x: i32,
        order_y: i32, kernel: [f32; 25], divisor: f32, bias: f32, target_x: i32,
        target_y: i32, kernel_unit_length_x: f32, kernel_unit_length_y: f32,
        preserve_alpha: i32 },
    /// transform image through convolution matrix of up to 25 values (spec
    /// allows more but for performance reasons we do not)
    /// parameters: FilterOpGraphNode, orderX, orderY, kernelValues[25],
    ///  divisor, bias, targetX, targetY, kernelUnitLengthX, kernelUnitLengthY,
    ///  preserveAlpha
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feConvolveMatrixElement
    SVGFEConvolveMatrixEdgeModeNone { node: FilterOpGraphNode, order_x: i32,
        order_y: i32, kernel: [f32; 25], divisor: f32, bias: f32, target_x: i32,
        target_y: i32, kernel_unit_length_x: f32, kernel_unit_length_y: f32,
        preserve_alpha: i32 },
    /// transform image through convolution matrix of up to 25 values (spec
    /// allows more but for performance reasons we do not)
    /// parameters: FilterOpGraphNode, orderX, orderY, kernelValues[25],
    ///  divisor, bias, targetX, targetY, kernelUnitLengthX, kernelUnitLengthY,
    /// preserveAlpha
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#feConvolveMatrixElement
    SVGFEConvolveMatrixEdgeModeWrap { node: FilterOpGraphNode, order_x: i32,
        order_y: i32, kernel: [f32; 25], divisor: f32, bias: f32, target_x: i32,
        target_y: i32, kernel_unit_length_x: f32, kernel_unit_length_y: f32,
        preserve_alpha: i32 },
    /// calculate lighting based on heightmap image with provided values for a
    /// distant light source with specified direction
    /// parameters: FilterOpGraphNode, surfaceScale, diffuseConstant,
    ///  kernelUnitLengthX, kernelUnitLengthY, azimuth, elevation
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEDiffuseLightingElement
    ///  https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEDistantLightElement
    SVGFEDiffuseLightingDistant { node: FilterOpGraphNode, surface_scale: f32,
        diffuse_constant: f32, kernel_unit_length_x: f32,
        kernel_unit_length_y: f32, azimuth: f32, elevation: f32 },
    /// calculate lighting based on heightmap image with provided values for a
    /// point light source at specified location
    /// parameters: FilterOpGraphNode, surfaceScale, diffuseConstant,
    ///  kernelUnitLengthX, kernelUnitLengthY, x, y, z
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEDiffuseLightingElement
    ///  https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEPointLightElement
    SVGFEDiffuseLightingPoint { node: FilterOpGraphNode, surface_scale: f32,
        diffuse_constant: f32, kernel_unit_length_x: f32,
        kernel_unit_length_y: f32, x: f32, y: f32, z: f32 },
    /// calculate lighting based on heightmap image with provided values for a
    /// spot light source at specified location pointing at specified target
    /// location with specified hotspot sharpness and cone angle
    /// parameters: FilterOpGraphNode, surfaceScale, diffuseConstant,
    ///  kernelUnitLengthX, kernelUnitLengthY, x, y, z, pointsAtX, pointsAtY,
    ///  pointsAtZ, specularExponent, limitingConeAngle
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEDiffuseLightingElement
    /// https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFESpotLightElement
    SVGFEDiffuseLightingSpot { node: FilterOpGraphNode, surface_scale: f32,
        diffuse_constant: f32, kernel_unit_length_x: f32,
        kernel_unit_length_y: f32, x: f32, y: f32, z: f32, points_at_x: f32,
        points_at_y: f32, points_at_z: f32, cone_exponent: f32,
        limiting_cone_angle: f32 },
    /// calculate a distorted version of first input image using offset values
    /// from second input image at specified intensity
    /// parameters: FilterOpGraphNode, scale, xChannelSelector, yChannelSelector
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEDisplacementMapElement
    SVGFEDisplacementMap { node: FilterOpGraphNode, scale: f32,
        x_channel_selector: u32, y_channel_selector: u32 },
    /// create and merge a dropshadow version of the specified image's alpha
    /// channel with specified offset and blur radius
    /// parameters: FilterOpGraphNode, flood_color, flood_opacity, dx, dy,
    ///  stdDeviationX, stdDeviationY
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEDropShadowElement
    SVGFEDropShadow { node: FilterOpGraphNode, color: ColorF, dx: f32, dy: f32,
        std_deviation_x: f32, std_deviation_y: f32 },
    /// synthesize a new image of specified size containing a solid color
    /// parameters: FilterOpGraphNode, color
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEFloodElement
    SVGFEFlood { node: FilterOpGraphNode, color: ColorF },
    /// create a blurred version of the input image
    /// parameters: FilterOpGraphNode, stdDeviationX, stdDeviationY
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEGaussianBlurElement
    SVGFEGaussianBlur { node: FilterOpGraphNode, std_deviation_x: f32, std_deviation_y: f32 },
    /// synthesize a new image based on a url (i.e. blob image source)
    /// parameters: FilterOpGraphNode, sampling_filter (see SamplingFilter in Types.h), transform
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEImageElement
    SVGFEImage { node: FilterOpGraphNode, sampling_filter: u32, matrix: [f32; 6] },
    /// create a new image based on the input image with the contour stretched
    /// outward (dilate operator)
    /// parameters: FilterOpGraphNode, radiusX, radiusY
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEMorphologyElement
    SVGFEMorphologyDilate { node: FilterOpGraphNode, radius_x: f32, radius_y: f32 },
    /// create a new image based on the input image with the contour shrunken
    /// inward (erode operator)
    /// parameters: FilterOpGraphNode, radiusX, radiusY
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEMorphologyElement
    SVGFEMorphologyErode { node: FilterOpGraphNode, radius_x: f32, radius_y: f32 },
    /// create a new image that is a scrolled version of the input image, this
    /// is basically a no-op as we support offset in the graph node
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEOffsetElement
    SVGFEOffset { node: FilterOpGraphNode, offset_x: f32, offset_y: f32 },
    /// calculate lighting based on heightmap image with provided values for a
    /// distant light source with specified direction
    /// parameters: FilerData, surfaceScale, specularConstant, specularExponent,
    ///  kernelUnitLengthX, kernelUnitLengthY, azimuth, elevation
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFESpecularLightingElement
    /// https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEDistantLightElement
    SVGFESpecularLightingDistant { node: FilterOpGraphNode, surface_scale: f32,
        specular_constant: f32, specular_exponent: f32,
        kernel_unit_length_x: f32, kernel_unit_length_y: f32, azimuth: f32,
        elevation: f32 },
    /// calculate lighting based on heightmap image with provided values for a
    /// point light source at specified location
    /// parameters: FilterOpGraphNode, surfaceScale, specularConstant,
    ///  specularExponent, kernelUnitLengthX, kernelUnitLengthY, x, y, z
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFESpecularLightingElement
    ///  https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFEPointLightElement
    SVGFESpecularLightingPoint { node: FilterOpGraphNode, surface_scale: f32,
        specular_constant: f32, specular_exponent: f32,
        kernel_unit_length_x: f32, kernel_unit_length_y: f32, x: f32, y: f32,
        z: f32 },
    /// calculate lighting based on heightmap image with provided values for a
    /// spot light source at specified location pointing at specified target
    /// location with specified hotspot sharpness and cone angle
    /// parameters: FilterOpGraphNode, surfaceScale, specularConstant,
    ///  specularExponent, kernelUnitLengthX, kernelUnitLengthY, x, y, z,
    ///  pointsAtX, pointsAtY, pointsAtZ, specularExponent, limitingConeAngle
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFESpecularLightingElement
    ///  https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFESpotLightElement
    SVGFESpecularLightingSpot { node: FilterOpGraphNode, surface_scale: f32,
        specular_constant: f32, specular_exponent: f32,
        kernel_unit_length_x: f32, kernel_unit_length_y: f32, x: f32, y: f32,
        z: f32, points_at_x: f32, points_at_y: f32, points_at_z: f32,
        cone_exponent: f32, limiting_cone_angle: f32 },
    /// create a new image based on the input image, repeated throughout the
    /// output rectangle
    /// parameters: FilterOpGraphNode
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFETileElement
    SVGFETile { node: FilterOpGraphNode },
    /// synthesize a new image based on Fractal Noise (Perlin) with the chosen
    /// stitching mode
    /// parameters: FilterOpGraphNode, baseFrequencyX, baseFrequencyY,
    ///  numOctaves, seed
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFETurbulenceElement
    SVGFETurbulenceWithFractalNoiseWithNoStitching { node: FilterOpGraphNode,
        base_frequency_x: f32, base_frequency_y: f32, num_octaves: u32,
        seed: u32 },
    /// synthesize a new image based on Fractal Noise (Perlin) with the chosen
    /// stitching mode
    /// parameters: FilterOpGraphNode, baseFrequencyX, baseFrequencyY,
    ///  numOctaves, seed
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFETurbulenceElement
    SVGFETurbulenceWithFractalNoiseWithStitching { node: FilterOpGraphNode,
        base_frequency_x: f32, base_frequency_y: f32, num_octaves: u32,
        seed: u32 },
    /// synthesize a new image based on Turbulence Noise (offset vectors)
    /// parameters: FilterOpGraphNode, baseFrequencyX, baseFrequencyY,
    ///  numOctaves, seed
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFETurbulenceElement
    SVGFETurbulenceWithTurbulenceNoiseWithNoStitching { node: FilterOpGraphNode,
        base_frequency_x: f32, base_frequency_y: f32, num_octaves: u32,
        seed: u32 },
    /// synthesize a new image based on Turbulence Noise (offset vectors)
    /// parameters: FilterOpGraphNode, baseFrequencyX, baseFrequencyY,
    ///  numOctaves, seed
    /// SVG filter semantics - selectable input(s), selectable between linear
    /// (default) and sRGB color space for calculations
    /// Spec: https://www.w3.org/TR/filter-effects-1/#InterfaceSVGFETurbulenceElement
    SVGFETurbulenceWithTurbulenceNoiseWithStitching { node: FilterOpGraphNode,
        base_frequency_x: f32, base_frequency_y: f32, num_octaves: u32, seed: u32 },
}

impl FilterOp {
    /// Mutable access to the SVGFE filter-graph node carried by every `SVGFE*`
    /// filter op (the `node` field), or `None` for non-SVGFE ops. The node's
    /// `subregion` is the only absolutely-positioned geometry an SVGFE op
    /// carries (in filter/layout space); every other SVGFE coordinate is
    /// relative. Used by the display-list builder to pre-normalize the
    /// subregion by the external scroll offset.
    pub fn svgfe_node_mut(&mut self) -> Option<&mut FilterOpGraphNode> {
        match self {
            FilterOp::SVGFEBlendColor { node, .. } |
            FilterOp::SVGFEBlendColorBurn { node, .. } |
            FilterOp::SVGFEBlendColorDodge { node, .. } |
            FilterOp::SVGFEBlendDarken { node, .. } |
            FilterOp::SVGFEBlendDifference { node, .. } |
            FilterOp::SVGFEBlendExclusion { node, .. } |
            FilterOp::SVGFEBlendHardLight { node, .. } |
            FilterOp::SVGFEBlendHue { node, .. } |
            FilterOp::SVGFEBlendLighten { node, .. } |
            FilterOp::SVGFEBlendLuminosity { node, .. } |
            FilterOp::SVGFEBlendMultiply { node, .. } |
            FilterOp::SVGFEBlendNormal { node, .. } |
            FilterOp::SVGFEBlendOverlay { node, .. } |
            FilterOp::SVGFEBlendSaturation { node, .. } |
            FilterOp::SVGFEBlendScreen { node, .. } |
            FilterOp::SVGFEBlendSoftLight { node, .. } |
            FilterOp::SVGFEColorMatrix { node, .. } |
            FilterOp::SVGFEComponentTransfer { node, .. } |
            FilterOp::SVGFECompositeArithmetic { node, .. } |
            FilterOp::SVGFECompositeATop { node, .. } |
            FilterOp::SVGFECompositeIn { node, .. } |
            FilterOp::SVGFECompositeLighter { node, .. } |
            FilterOp::SVGFECompositeOut { node, .. } |
            FilterOp::SVGFECompositeOver { node, .. } |
            FilterOp::SVGFECompositeXOR { node, .. } |
            FilterOp::SVGFEConvolveMatrixEdgeModeDuplicate { node, .. } |
            FilterOp::SVGFEConvolveMatrixEdgeModeNone { node, .. } |
            FilterOp::SVGFEConvolveMatrixEdgeModeWrap { node, .. } |
            FilterOp::SVGFEDiffuseLightingDistant { node, .. } |
            FilterOp::SVGFEDiffuseLightingPoint { node, .. } |
            FilterOp::SVGFEDiffuseLightingSpot { node, .. } |
            FilterOp::SVGFEDisplacementMap { node, .. } |
            FilterOp::SVGFEDropShadow { node, .. } |
            FilterOp::SVGFEFlood { node, .. } |
            FilterOp::SVGFEGaussianBlur { node, .. } |
            FilterOp::SVGFEIdentity { node, .. } |
            FilterOp::SVGFEImage { node, .. } |
            FilterOp::SVGFEMorphologyDilate { node, .. } |
            FilterOp::SVGFEMorphologyErode { node, .. } |
            FilterOp::SVGFEOffset { node, .. } |
            FilterOp::SVGFEOpacity { node, .. } |
            FilterOp::SVGFESourceAlpha { node, .. } |
            FilterOp::SVGFESourceGraphic { node, .. } |
            FilterOp::SVGFESpecularLightingDistant { node, .. } |
            FilterOp::SVGFESpecularLightingPoint { node, .. } |
            FilterOp::SVGFESpecularLightingSpot { node, .. } |
            FilterOp::SVGFETile { node, .. } |
            FilterOp::SVGFEToAlpha { node, .. } |
            FilterOp::SVGFETurbulenceWithFractalNoiseWithNoStitching { node, .. } |
            FilterOp::SVGFETurbulenceWithFractalNoiseWithStitching { node, .. } |
            FilterOp::SVGFETurbulenceWithTurbulenceNoiseWithNoStitching { node, .. } |
            FilterOp::SVGFETurbulenceWithTurbulenceNoiseWithStitching { node, .. }
 => Some(node),
            _ => None,
        }
    }
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, PartialEq, Deserialize, Serialize, PeekPoke)]
pub enum ComponentTransferFuncType {
  Identity = 0,
  Table = 1,
  Discrete = 2,
  Linear = 3,
  Gamma = 4,
}

#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct FilterData {
    /// ComponentTransfer / SVGFEComponentTransfer
    pub func_r_type: ComponentTransferFuncType,
    pub r_values: Vec<f32>,
    pub func_g_type: ComponentTransferFuncType,
    pub g_values: Vec<f32>,
    pub func_b_type: ComponentTransferFuncType,
    pub b_values: Vec<f32>,
    pub func_a_type: ComponentTransferFuncType,
    pub a_values: Vec<f32>,
}

fn sanitize_func_type(
    func_type: ComponentTransferFuncType,
    values: &[f32],
) -> ComponentTransferFuncType {
    if values.is_empty() {
        return ComponentTransferFuncType::Identity;
    }
    if values.len() < 2 && func_type == ComponentTransferFuncType::Linear {
        return ComponentTransferFuncType::Identity;
    }
    if values.len() < 3 && func_type == ComponentTransferFuncType::Gamma {
        return ComponentTransferFuncType::Identity;
    }
    func_type
}

fn sanitize_values(
    func_type: ComponentTransferFuncType,
    values: &[f32],
) -> bool {
    if values.len() < 2 && func_type == ComponentTransferFuncType::Linear {
        return false;
    }
    if values.len() < 3 && func_type == ComponentTransferFuncType::Gamma {
        return false;
    }
    true
}

impl FilterData {
    /// Ensure that the number of values matches up with the function type.
    pub fn sanitize(&self) -> FilterData {
        FilterData {
            func_r_type: sanitize_func_type(self.func_r_type, &self.r_values),
            r_values:
                    if sanitize_values(self.func_r_type, &self.r_values) {
                        self.r_values.clone()
                    } else {
                        Vec::new()
                    },
            func_g_type: sanitize_func_type(self.func_g_type, &self.g_values),
            g_values:
                    if sanitize_values(self.func_g_type, &self.g_values) {
                        self.g_values.clone()
                    } else {
                        Vec::new()
                    },

            func_b_type: sanitize_func_type(self.func_b_type, &self.b_values),
            b_values:
                    if sanitize_values(self.func_b_type, &self.b_values) {
                        self.b_values.clone()
                    } else {
                        Vec::new()
                    },

            func_a_type: sanitize_func_type(self.func_a_type, &self.a_values),
            a_values:
                    if sanitize_values(self.func_a_type, &self.a_values) {
                        self.a_values.clone()
                    } else {
                        Vec::new()
                    },

        }
    }

    pub fn is_identity(&self) -> bool {
        self.func_r_type == ComponentTransferFuncType::Identity &&
        self.func_g_type == ComponentTransferFuncType::Identity &&
        self.func_b_type == ComponentTransferFuncType::Identity &&
        self.func_a_type == ComponentTransferFuncType::Identity
    }
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct IframeDisplayItem {
    pub bounds: LayoutRect,
    pub clip_rect: LayoutRect,
    pub space_and_clip: SpaceAndClipInfo,
    pub pipeline_id: PipelineId,
    pub ignore_missing_pipeline: bool,
}

/// This describes an image that fills the specified area. It stretches or shrinks
/// the image as necessary. While RepeatingImageDisplayItem could otherwise provide
/// a superset of the functionality, it has been problematic inferring the desired
/// repetition properties when snapping changes the size of the primitive.
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ImageDisplayItem {
    pub common: CommonItemProperties,
    /// The area to tile the image over (first tile starts at origin of this rect)
    // FIXME: this should ideally just be `tile_origin` here, with the clip_rect
    // defining the bounds of the item. Needs non-trivial backend changes.
    pub bounds: LayoutRect,
    pub image_key: ImageKey,
    pub image_rendering: ImageRendering,
    pub alpha_type: AlphaType,
    /// A hack used by gecko to color a simple bitmap font used for tofu glyphs
    pub color: ColorF,
}

/// This describes a background-image and its tiling. It repeats in a grid to fill
/// the specified area.
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct RepeatingImageDisplayItem {
    pub common: CommonItemProperties,
    /// The area to tile the image over (first tile starts at origin of this rect)
    // FIXME: this should ideally just be `tile_origin` here, with the clip_rect
    // defining the bounds of the item. Needs non-trivial backend changes.
    pub bounds: LayoutRect,
    /// How large to make a single tile of the image (common case: bounds.size)
    pub stretch_size: LayoutSize,
    /// The space between tiles (common case: 0)
    pub tile_spacing: LayoutSize,
    pub image_key: ImageKey,
    pub image_rendering: ImageRendering,
    pub alpha_type: AlphaType,
    /// A hack used by gecko to color a simple bitmap font used for tofu glyphs
    pub color: ColorF,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum ImageRendering {
    Auto = 0,
    CrispEdges = 1,
    Pixelated = 2,
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum AlphaType {
    Alpha = 0,
    PremultipliedAlpha = 1,
}

#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct YuvImageDisplayItem {
    pub common: CommonItemProperties,
    pub bounds: LayoutRect,
    pub yuv_data: YuvData,
    pub color_depth: ColorDepth,
    pub color_space: YuvColorSpace,
    pub color_range: ColorRange,
    pub image_rendering: ImageRendering,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum YuvColorSpace {
    Rec601 = 0,
    Rec709 = 1,
    Rec2020 = 2,
    Identity = 3, // aka GBR as per ISO/IEC 23091-2:2019
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum ColorRange {
    Limited = 0,
    Full = 1,
}

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum YuvRangedColorSpace {
    Rec601Narrow = 0,
    Rec601Full = 1,
    Rec709Narrow = 2,
    Rec709Full = 3,
    Rec2020Narrow = 4,
    Rec2020Full = 5,
    GbrIdentity = 6,
}

impl YuvColorSpace {
    pub fn with_range(self, range: ColorRange) -> YuvRangedColorSpace {
        match self {
            YuvColorSpace::Identity => YuvRangedColorSpace::GbrIdentity,
            YuvColorSpace::Rec601 => {
                match range {
                    ColorRange::Limited => YuvRangedColorSpace::Rec601Narrow,
                    ColorRange::Full => YuvRangedColorSpace::Rec601Full,
                }
            }
            YuvColorSpace::Rec709 => {
                match range {
                    ColorRange::Limited => YuvRangedColorSpace::Rec709Narrow,
                    ColorRange::Full => YuvRangedColorSpace::Rec709Full,
                }
            }
            YuvColorSpace::Rec2020 => {
                match range {
                    ColorRange::Limited => YuvRangedColorSpace::Rec2020Narrow,
                    ColorRange::Full => YuvRangedColorSpace::Rec2020Full,
                }
            }
        }
    }
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq, Serialize, PeekPoke)]
pub enum YuvData {
    NV12(ImageKey, ImageKey), // (Y channel, CbCr interleaved channel)
    P010(ImageKey, ImageKey), // (Y channel, CbCr interleaved channel)
    NV16(ImageKey, ImageKey), // (Y channel, CbCr interleaved channel)
    P210(ImageKey, ImageKey), // (Y channel, CbCr interleaved channel)
    PlanarYCbCr(ImageKey, ImageKey, ImageKey), // (Y channel, Cb channel, Cr Channel)
    InterleavedYCbCr(ImageKey), // (YCbCr interleaved channel)
}

impl YuvData {
    pub fn get_format(&self) -> YuvFormat {
        match *self {
            YuvData::NV12(..) => YuvFormat::NV12,
            YuvData::P010(..) => YuvFormat::P010,
            YuvData::NV16(..) => YuvFormat::NV16,
            YuvData::P210(..) => YuvFormat::P210,
            YuvData::PlanarYCbCr(..) => YuvFormat::PlanarYCbCr,
            YuvData::InterleavedYCbCr(..) => YuvFormat::InterleavedYCbCr,
        }
    }
}

#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub enum YuvFormat {
    // These enum values need to be kept in sync with yuv.glsl.
    NV12 = 0,
    P010 = 1,
    NV16 = 2,
    P210 = 3,
    PlanarYCbCr = 4,
    InterleavedYCbCr = 5,
}

impl YuvFormat {
    pub fn get_plane_num(self) -> usize {
        match self {
            YuvFormat::NV12
            | YuvFormat::P010
            | YuvFormat::NV16
            | YuvFormat::P210 => 2,
            YuvFormat::PlanarYCbCr => 3,
            YuvFormat::InterleavedYCbCr => 1,
        }
    }

    pub fn is_msb_aligned(self) -> bool {
        match self {
            YuvFormat::NV12
            | YuvFormat::NV16
            | YuvFormat::PlanarYCbCr
            | YuvFormat::InterleavedYCbCr => false,
            YuvFormat::P010 | YuvFormat::P210 => true,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ImageMask {
    pub image: ImageKey,
    pub rect: LayoutRect,
}

impl ImageMask {
    /// Get a local clipping rect contributed by this mask.
    pub fn get_local_clip_rect(&self) -> Option<LayoutRect> {
        Some(self.rect)
    }
}

#[repr(C)]
#[derive(Copy, Clone, Debug, MallocSizeOf, PartialEq, Serialize, Deserialize, Eq, Hash, PeekPoke)]
pub enum ClipMode {
    Clip,    // Pixels inside the region are visible.
    ClipOut, // Pixels outside the region are visible.
}

impl Not for ClipMode {
    type Output = ClipMode;

    fn not(self) -> ClipMode {
        match self {
            ClipMode::Clip => ClipMode::ClipOut,
            ClipMode::ClipOut => ClipMode::Clip,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, Deserialize, PartialEq, Serialize, PeekPoke)]
pub struct ComplexClipRegion {
    /// The boundaries of the rectangle.
    pub rect: LayoutRect,
    /// Border radii of this rectangle.
    pub radii: BorderRadius,
    /// Accumulated inset from the reference shape, in case of inflation/deflation.
    /// The inset is expected to be already baked in rect and radii, but is necessary
    /// for the correct computation of corner shapes.
    /// Example: a shape with a border of 10px should have a 10px inset applied on its
    /// content mask.
    pub inset: LayoutSideOffsets,
    /// Whether we are clipping inside or outside
    /// the region.
    pub mode: ClipMode,
}

impl BorderRadius {
    pub fn zero() -> BorderRadius {
        BorderRadius {
            top_left: LayoutSize::new(0.0, 0.0),
            top_right: LayoutSize::new(0.0, 0.0),
            bottom_left: LayoutSize::new(0.0, 0.0),
            bottom_right: LayoutSize::new(0.0, 0.0),
            shape_top_left: 1.0,
            shape_top_right: 1.0,
            shape_bottom_left: 1.0,
            shape_bottom_right: 1.0,
        }
    }

    pub fn uniform(radius: f32) -> BorderRadius {
        BorderRadius {
            top_left: LayoutSize::new(radius, radius),
            top_right: LayoutSize::new(radius, radius),
            bottom_left: LayoutSize::new(radius, radius),
            bottom_right: LayoutSize::new(radius, radius),
            shape_top_left: 1.0,
            shape_top_right: 1.0,
            shape_bottom_left: 1.0,
            shape_bottom_right: 1.0,
        }
    }

    pub fn uniform_size(radius: LayoutSize) -> BorderRadius {
        BorderRadius {
            top_left: radius,
            top_right: radius,
            bottom_left: radius,
            bottom_right: radius,
            shape_top_left: 1.0,
            shape_top_right: 1.0,
            shape_bottom_left: 1.0,
            shape_bottom_right: 1.0,
        }
    }

    pub fn all_sides_uniform(&self) -> bool {
        let corner_is_uniform = |corner: &LayoutSize| corner.width == corner.height;
        corner_is_uniform(&self.top_left) &&
        corner_is_uniform(&self.top_right) &&
        corner_is_uniform(&self.bottom_right) &&
        corner_is_uniform(&self.bottom_left)
    }

    pub fn can_use_fast_path_in(&self, rect: &LayoutRect) -> bool {
        if !self.shapes_all_round() {
            return false;
        }
        if !self.all_sides_uniform() {
            // The fast path needs uniform sides.
            return false;
        }
        // The shader code that evaluates the rounded corners in the fast path relies on each
        // corner fitting into their quadrant of the quad. In other words the radius cannot
        // exceed half of the length of the sides they are on. That necessarily holds if all the
        // radii are the same.
        let tl = self.top_left.width;
        if tl == self.bottom_right.width && tl == self.top_right.width && tl == self.bottom_left.width {
            return true;
        }
        let half_size = rect.size() * 0.5;
        let fits = |v: f32| v <= half_size.width && v <= half_size.height;
        fits(tl) && fits(self.bottom_right.width) && fits(self.top_right.width) && fits(self.bottom_left.width)
    }

    /// Return whether, in each corner, the radius in *either* direction is zero.
    /// This means that none of the corners are rounded.
    pub fn is_zero(&self) -> bool {
        let corner_is_zero = |corner: &LayoutSize| corner.width == 0.0 || corner.height == 0.0;
        corner_is_zero(&self.top_left) &&
        corner_is_zero(&self.top_right) &&
        corner_is_zero(&self.bottom_right) &&
        corner_is_zero(&self.bottom_left)
    }
}

impl ComplexClipRegion {
    /// Create a new complex clip region.
    pub fn new(
        rect: LayoutRect,
        radii: BorderRadius,
        inset: LayoutSideOffsets,
        mode: ClipMode,
    ) -> Self {
        ComplexClipRegion { rect, radii, inset, mode }
    }
}

impl ComplexClipRegion {
    /// Get a local clipping rect contributed by this clip region.
    pub fn get_local_clip_rect(&self) -> Option<LayoutRect> {
        match self.mode {
            ClipMode::Clip => {
                Some(self.rect)
            }
            ClipMode::ClipOut => {
                None
            }
        }
    }
}

pub const POLYGON_CLIP_VERTEX_MAX: usize = 32;

#[repr(u8)]
#[derive(Clone, Copy, Debug, Deserialize, MallocSizeOf, PartialEq, Serialize, Eq, Hash, PeekPoke)]
pub enum FillRule {
    Nonzero = 0x1, // Behaves as the SVG fill-rule definition for nonzero.
    Evenodd = 0x2, // Behaves as the SVG fill-rule definition for evenodd.
}

impl From<u8> for FillRule {
    fn from(fill_rule: u8) -> Self {
        match fill_rule {
            0x1 => FillRule::Nonzero,
            0x2 => FillRule::Evenodd,
            _ => panic!("Unexpected FillRule value."),
        }
    }
}

impl From<FillRule> for u8 {
    fn from(fill_rule: FillRule) -> Self {
        match fill_rule {
            FillRule::Nonzero => 0x1,
            FillRule::Evenodd => 0x2,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, Deserialize, Eq, Hash, PartialEq, Serialize, PeekPoke)]
pub struct ClipChainId(pub u64, pub PipelineId);

impl ClipChainId {
    pub const INVALID: Self = ClipChainId(!0, PipelineId::INVALID);
}

/// A reference to a clipping node defining how an item is clipped.
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, PartialEq, Serialize, PeekPoke)]
pub struct ClipId(pub usize, pub PipelineId);

impl Default for ClipId {
    fn default() -> Self {
        ClipId::invalid()
    }
}

const ROOT_CLIP_ID: usize = 0;

impl ClipId {
    /// Return the root clip ID - effectively doing no clipping.
    pub fn root(pipeline_id: PipelineId) -> Self {
        ClipId(ROOT_CLIP_ID, pipeline_id)
    }

    /// Return an invalid clip ID - needed in places where we carry
    /// one but need to not attempt to use it.
    pub fn invalid() -> Self {
        ClipId(!0, PipelineId::dummy())
    }

    pub fn pipeline_id(&self) -> PipelineId {
        match *self {
            ClipId(_, pipeline_id) => pipeline_id,
        }
    }

    pub fn is_root(&self) -> bool {
        match *self {
            ClipId(id, _) => id == ROOT_CLIP_ID,
        }
    }

    pub fn is_valid(&self) -> bool {
        match *self {
            ClipId(id, _) => id != !0,
        }
    }
}

/// A reference to a spatial node defining item positioning.
#[derive(Clone, Copy, Debug, Default, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
pub struct SpatialId(pub usize, PipelineId);

const ROOT_REFERENCE_FRAME_SPATIAL_ID: usize = 0;
const ROOT_SCROLL_NODE_SPATIAL_ID: usize = 1;

impl SpatialId {
    pub fn new(spatial_node_index: usize, pipeline_id: PipelineId) -> Self {
        SpatialId(spatial_node_index, pipeline_id)
    }

    pub fn root_reference_frame(pipeline_id: PipelineId) -> Self {
        SpatialId(ROOT_REFERENCE_FRAME_SPATIAL_ID, pipeline_id)
    }

    pub fn root_scroll_node(pipeline_id: PipelineId) -> Self {
        SpatialId(ROOT_SCROLL_NODE_SPATIAL_ID, pipeline_id)
    }

    pub fn pipeline_id(&self) -> PipelineId {
        self.1
    }
}

/// An external identifier that uniquely identifies a scroll frame independent of its ClipId, which
/// may change from frame to frame. This should be unique within a pipeline. WebRender makes no
/// attempt to ensure uniqueness. The zero value is reserved for use by the root scroll node of
/// every pipeline, which always has an external id.
///
/// When setting display lists with the `preserve_frame_state` this id is used to preserve scroll
/// offsets between different sets of SpatialNodes which are ScrollFrames.
#[derive(Clone, Copy, Debug, Default, Deserialize, Eq, Hash, MallocSizeOf, PartialEq, Serialize, PeekPoke)]
#[repr(C)]
pub struct ExternalScrollId(pub u64, pub PipelineId);

impl ExternalScrollId {
    pub fn pipeline_id(&self) -> PipelineId {
        self.1
    }

    pub fn is_root(&self) -> bool {
        self.0 == 0
    }
}

impl DisplayItem {
    pub fn debug_name(&self) -> &'static str {
        match *self {
            DisplayItem::Border(..) => "border",
            DisplayItem::BoxShadow(..) => "box_shadow",
            DisplayItem::HitTest(..) => "hit_test",
            DisplayItem::RectClip(..) => "rect_clip",
            DisplayItem::RoundedRectClip(..) => "rounded_rect_clip",
            DisplayItem::ImageMaskClip(..) => "image_mask_clip",
            DisplayItem::ClipChain(..) => "clip_chain",
            DisplayItem::ConicGradient(..) => "conic_gradient",
            DisplayItem::Gradient(..) => "gradient",
            DisplayItem::Iframe(..) => "iframe",
            DisplayItem::Image(..) => "image",
            DisplayItem::RepeatingImage(..) => "repeating_image",
            DisplayItem::Line(..) => "line",
            DisplayItem::PopReferenceFrame => "pop_reference_frame",
            DisplayItem::PopStackingContext => "pop_stacking_context",
            DisplayItem::PushReferenceFrame(..) => "push_reference_frame",
            DisplayItem::PushStackingContext(..) => "push_stacking_context",
            DisplayItem::SetFilterOps => "set_filter_ops",
            DisplayItem::SetFilterData => "set_filter_data",
            DisplayItem::SetPoints => "set_points",
            DisplayItem::RadialGradient(..) => "radial_gradient",
            DisplayItem::Rectangle(..) => "rectangle",
            DisplayItem::SetGradientStops => "set_gradient_stops",
            DisplayItem::Text(..) => "text",
            DisplayItem::YuvImage(..) => "yuv_image",
            DisplayItem::BackdropFilter(..) => "backdrop_filter",
            DisplayItem::DebugMarker(..) => "debug",
        }
    }
}

macro_rules! impl_default_for_enums {
    ($($enum:ident => $init:expr ),+) => {
        $(impl Default for $enum {
            #[allow(unused_imports)]
            fn default() -> Self {
                use $enum::*;
                $init
            }
        })*
    }
}

impl_default_for_enums! {
    DisplayItem => PopStackingContext,
    LineOrientation => Vertical,
    LineStyle => Solid,
    RepeatMode => Stretch,
    NinePatchBorderSource => Image(ImageKey::default(), ImageRendering::Auto),
    BorderDetails => Normal(NormalBorder::default()),
    BorderRadiusKind => Uniform,
    BorderStyle => None,
    BoxShadowClipMode => Outset,
    ExtendMode => Clamp,
    FilterOp => Identity,
    ComponentTransferFuncType => Identity,
    ClipMode => Clip,
    FillRule => Nonzero,
    ReferenceFrameKind => Transform {
        is_2d_scale_translation: false,
        should_snap: false,
        paired_with_perspective: false,
    },
    Rotation => Degree0,
    TransformStyle => Flat,
    RasterSpace => Local(f32::default()),
    MixBlendMode => Normal,
    ImageRendering => Auto,
    AlphaType => Alpha,
    YuvColorSpace => Rec601,
    YuvRangedColorSpace => Rec601Narrow,
    ColorRange => Limited,
    YuvData => NV12(ImageKey::default(), ImageKey::default()),
    YuvFormat => NV12,
    FilterPrimitiveInput => Original,
    ColorSpace => Srgb,
    CompositeOperator => Over
}
