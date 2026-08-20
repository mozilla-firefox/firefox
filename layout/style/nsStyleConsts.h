/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* constants used in the style struct data provided by ComputedStyle */

#ifndef nsStyleConsts_h_
#define nsStyleConsts_h_

#include "X11UndefineNone.h"
#include "gfxFontConstants.h"
#include "mozilla/ServoStyleConsts.h"

// XXX fold this into ComputedStyle and group by nsStyleXXX struct

namespace mozilla {

// box-align
enum class StyleBoxAlign : uint8_t {
  Stretch,
  Start,
  Center,
  Baseline,
  End,
};

// box-decoration-break
enum class StyleBoxDecorationBreak : uint8_t {
  Slice,
  Clone,
};

// box-direction
enum class StyleBoxDirection : uint8_t {
  Normal,
  Reverse,
};

// box-orient
enum class StyleBoxOrient : uint8_t {
  Horizontal,
  Vertical,
};

// box-pack
enum class StyleBoxPack : uint8_t {
  Start,
  Center,
  End,
  Justify,
};

// box-sizing
enum class StyleBoxSizing : uint8_t { ContentBox, BorderBox };

// box-shadow
enum class StyleBoxShadowType : uint8_t {
  Inset,
};

enum class StyleColumnFill : uint8_t {
  Balance,
  Auto,
};

enum class StyleColumnSpan : uint8_t {
  None,
  All,
};

// Define geometry box for clip-path's reference-box, background-clip,
// background-origin, mask-clip, mask-origin, shape-box and transform-box.
enum class StyleGeometryBox : uint8_t {
  ContentBox,  // Used by everything, except transform-box.
  PaddingBox,  // Used by everything, except transform-box.
  BorderBox,
  MarginBox,   // clip-path reference-box only.
  FillBox,     // Used by everything, except shape-box.
  StrokeBox,   // mask-clip, mask-origin and clip-path reference-box only.
  ViewBox,     // Used by everything, except shape-box.
  NoClip,      // mask-clip only.
  Text,        // background-clip only.
  BorderArea,  // The area painted by the border. background-clip only.
  NoBox,  // Depending on which kind of element this style value applied on,
          // the default value of a reference-box can be different.
          // For an HTML element, the default value of reference-box is
          // border-box; for an SVG element, the default value is fill-box.
          // Since we can not determine the default value at parsing time,
          // set it as NoBox so that we make a decision later.
          // clip-path reference-box only.
  MozAlmostPadding = 127  // A magic value that we use for our "pretend that
                          // background-clip is 'padding' when we have a solid
                          // border" optimization.  This isn't actually equal
                          // to StyleGeometryBox::Padding because using that
                          // causes antialiasing seams between the background
                          // and border.
                          // background-clip only.
};

// float-edge
enum class StyleFloatEdge : uint8_t {
  ContentBox,
  MarginBox,
};

// Hyphens
enum class StyleHyphens : uint8_t {
  None,
  Manual,
  Auto,
};

// image-orientation
enum class StyleImageOrientation : uint8_t {
  None,
  FromImage,
};

// scrollbar-width
enum class StyleScrollbarWidth : uint8_t {
  Auto,
  Thin,
  None,
};

// field-sizing
enum class StyleFieldSizing : bool {
  Fixed,
  Content,
};

// Shape source type
enum class StyleShapeSourceType : uint8_t {
  None,
  Image,  // shape-outside / clip-path only, and clip-path only uses it for
          // <url>s
  Shape,
  Box,
  Path,  // SVG path function
};

// -moz-window-dragging
enum class StyleWindowDragging : uint8_t {
  Default,
  Drag,
  NoDrag,
};

// orient
enum class StyleOrient : uint8_t {
  Inline,
  Block,
  Horizontal,
  Vertical,
};

// See nsStyleImageLayers
enum class StyleImageLayerAttachment : uint8_t { Scroll, Fixed, Local };

// See nsStyleImageLayers
enum class StyleImageLayerRepeat : uint8_t {
  NoRepeat = 0x00,
  RepeatX,
  RepeatY,
  Repeat,
  Space,
  Round
};

// Mask mode
enum class StyleMaskMode : uint8_t { Alpha = 0, Luminance, MatchSource };

// See nsStyleTable
enum class StyleBorderCollapse : uint8_t { Collapse, Separate };

// See nsStyleVisibility
enum class StyleDirection : uint8_t { Ltr, Rtl };

// See nsStyleVisibility
// NOTE: WritingModes.h depends on the particular values used here.

// Single-bit flag, used in combination with VerticalLR and RL to specify
// the corresponding Sideways* modes.
// (To avoid ambiguity, this bit must be high enough such that no other
// values here accidentally use it in their binary representation.)
static constexpr uint8_t kWritingModeSidewaysMask = 4;

// See nsStylePosition
enum class StyleFlexDirection : uint8_t {
  Row,
  RowReverse,
  Column,
  ColumnReverse,
};

// CSS Grid <track-breadth> keywords
enum class StyleGridTrackBreadth : uint8_t {
  MaxContent = 1,
  MinContent = 2,
};

// defaults per MathML spec
static constexpr float kMathMLDefaultScriptSizeMultiplier{0.71f};
static constexpr float kMathMLDefaultScriptMinSizePt{8.f};

// See nsStyleFont
enum class StyleMathVariant : uint8_t {
  None = 0,
  Normal = 1,
  Bold = 2,
  Italic = 3,
  BoldItalic = 4,
  Script = 5,
  BoldScript = 6,
  Fraktur = 7,
  DoubleStruck = 8,
  BoldFraktur = 9,
  SansSerif = 10,
  BoldSansSerif = 11,
  SansSerifItalic = 12,
  SansSerifBoldItalic = 13,
  Monospace = 14,
  Initial = 15,
  Tailed = 16,
  Looped = 17,
  Stretched = 18,
};

// See nsStyleFont::mMathStyle
enum class StyleMathStyle : uint8_t { Compact = 0, Normal = 1 };

// See nsStyleFont::mMathShift
enum class StyleMathShift : uint8_t { Compact = 0, Normal = 1 };

enum class FrameBorderProperty : uint8_t { Yes, No, One, Zero };

enum class ScrollingAttribute : uint8_t {
  Yes,
  No,
  On,
  Off,
  Scroll,
  Noscroll,
  Auto
};

// See nsStyleList
enum class ListStyle : uint8_t {
  Custom = 255,  // for @counter-style
  None = 0,
  Decimal,
  Disc,
  Circle,
  Square,
  DisclosureClosed,
  DisclosureOpen,
  Hebrew,
  JapaneseInformal,
  JapaneseFormal,
  KoreanHangulFormal,
  KoreanHanjaInformal,
  KoreanHanjaFormal,
  SimpChineseInformal,
  SimpChineseFormal,
  TradChineseInformal,
  TradChineseFormal,
  EthiopicNumeric,
  // These styles are handled as custom styles defined in counterstyles.css.
  // They are preserved here only for html attribute map.
  LowerRoman = 100,
  UpperRoman,
  LowerAlpha,
  UpperAlpha
};

// See nsStyleList
enum class StyleListStylePosition : uint8_t { Inside, Outside };

enum class StyleIsolation : uint8_t {
  Auto,
  Isolate,
};

// See nsStylePosition.mObjectFit
enum class StyleObjectFit : uint8_t {
  Fill,
  Contain,
  Cover,
  None,
  ScaleDown,
};

// See nsStyleText
enum class StyleTextDecorationStyle : uint8_t {
  None,  // not in CSS spec, mapped to -moz-none
  Dotted,
  Dashed,
  Solid,
  Double,
  Wavy,
  Sentinel = Wavy
};

// See nsStyleText
enum class StyleTextSecurity : uint8_t {
  None,
  Circle,
  Disc,
  Square,
};

// See nsStyleDisplay
enum class StyleTopLayer : uint8_t {
  None,
  Auto,
};

// See nsStyleVisibility
enum class StyleVisibility : uint8_t {
  Hidden,
  Visible,
  Collapse,
};

// See nsStyleText
enum class StyleWhiteSpaceCollapse : uint8_t {
  Collapse = 0,
  // TODO: Discard not yet supported
  Preserve,
  PreserveBreaks,
  PreserveSpaces,
  BreakSpaces,
};

// See nsStyleText
enum class StyleTextWrapMode : uint8_t {
  Wrap = 0,
  Nowrap,
};

// See nsStyleText
// TODO: this will become StyleTextWrapStyle when we turn text-wrap
// (see https://bugzilla.mozilla.org/show_bug.cgi?id=1758391) and
// white-space (https://bugzilla.mozilla.org/show_bug.cgi?id=1852478)
// into shorthands.
enum class StyleTextWrapStyle : uint8_t {
  Auto = 0,
  Stable,
  Balance,
};

// ruby-align, see nsStyleText
enum class StyleRubyAlign : uint8_t {
  Start,
  Center,
  SpaceBetween,
  SpaceAround,
};

// See nsStyleText
enum class StyleTextSizeAdjust : uint8_t {
  None,
  Auto,
};

// See nsStyleVisibility
enum class StyleTextOrientation : uint8_t {
  Mixed,
  Upright,
  Sideways,
};

// Whether flexbox visibility: collapse items use legacy -moz-box behavior or
// not.
enum class StyleBoxCollapse : uint8_t {
  Flex,
  Legacy,
};

// See nsStyleText
enum class StyleTextCombineUpright : uint8_t {
  None,
  All,
};

// See nsStyleText
enum class StyleUnicodeBidi : uint8_t {
  Normal,
  Embed,
  Isolate,
  BidiOverride,
  IsolateOverride,
  Plaintext
};

enum class StyleTableLayout : uint8_t {
  Auto,
  Fixed,
};

enum class StyleEmptyCells : uint8_t {
  Hide,
  Show,
};

// See nsStyleUIReset
enum class StyleImeMode : uint8_t {
  Auto,
  Normal,
  Active,
  Disabled,
  Inactive,
};

// See nsStyleSVG

// -moz-window-shadow
enum class StyleWindowShadow : uint8_t {
  Auto,
  None,
};

// mask-type
enum class StyleMaskType : uint8_t {
  Luminance,
  Alpha,
};

// shape-rendering
enum class StyleShapeRendering : uint8_t {
  Auto,
  Optimizespeed,
  Crispedges,
  Geometricprecision,
};

// stroke-linecap
enum class StyleStrokeLinecap : uint8_t {
  Butt,
  Round,
  Square,
};

// stroke-linejoin
enum class StyleStrokeLinejoin : uint8_t {
  Miter,
  Round,
  Bevel,
};

// text-anchor
enum class StyleTextAnchor : uint8_t {
  Start,
  Middle,
  End,
};

// text-rendering
enum class StyleTextRendering : uint8_t {
  Auto,
  Optimizespeed,
  Optimizelegibility,
  Geometricprecision,
};

// color-interpolation and color-interpolation-filters
enum class StyleColorInterpolation : uint8_t {
  Auto = 0,
  Srgb = 1,
  Linearrgb = 2,
};

// 3d Transforms - Backface visibility
enum class StyleBackfaceVisibility : uint8_t { Hidden = 0, Visible = 1 };

// blending
enum class StyleBlend : uint8_t {
  Normal = 0,
  Multiply,
  Screen,
  Overlay,
  Darken,
  Lighten,
  ColorDodge,
  ColorBurn,
  HardLight,
  SoftLight,
  Difference,
  Exclusion,
  Hue,
  Saturation,
  Color,
  Luminosity,
  PlusLighter,
};

// composite
enum class StyleMaskComposite : uint8_t {
  Add = 0,
  Subtract,
  Intersect,
  Exclude
};

// scroll-behavior
enum class StyleScrollBehavior : uint8_t {
  Auto,
  Smooth,
};

}  // namespace mozilla

#endif /* nsStyleConsts_h_ */
