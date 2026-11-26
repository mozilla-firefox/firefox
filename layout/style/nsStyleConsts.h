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

// Define geometry box for clip-path's reference-box, shape-box and
// transform-box, and the resolved box of the background / mask layers.
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
  BorderAreaText,
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

// Shape source type
enum class StyleShapeSourceType : uint8_t {
  None,
  Image,  // shape-outside / clip-path only, and clip-path only uses it for
          // <url>s
  Shape,
  Box,
  Path,  // SVG path function
};

// See nsStyleImageLayers
enum class StyleImageLayerRepeat : uint8_t {
  NoRepeat = 0x00,
  RepeatX,
  RepeatY,
  Repeat,
  Space,
  Round
};

// See nsStyleVisibility
// NOTE: WritingModes.h depends on the particular values used here.

// Single-bit flag, used in combination with VerticalLR and RL to specify
// the corresponding Sideways* modes.
// (To avoid ambiguity, this bit must be high enough such that no other
// values here accidentally use it in their binary representation.)
static constexpr uint8_t kWritingModeSidewaysMask = 4;

// CSS Grid <track-breadth> keywords
enum class StyleGridTrackBreadth : uint8_t {
  MaxContent = 1,
  MinContent = 2,
};

// defaults per MathML spec
static constexpr float kMathMLDefaultScriptSizeMultiplier{0.71f};
static constexpr float kMathMLDefaultScriptMinSizePt{8.f};

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

// See nsStyleSVG

}  // namespace mozilla

#endif /* nsStyleConsts_h_ */
