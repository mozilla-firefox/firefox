/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCSSRenderingBorders.h"

#include <algorithm>

#include "BorderConsts.h"
#include "DashedCornerFinder.h"
#include "DottedCornerFinder.h"
#include "ImageRegion.h"
#include "gfx2DGlue.h"
#include "gfxGradientCache.h"
#include "gfxUtils.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/Range.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/image/WebRenderImageProvider.h"
#include "mozilla/layers/RenderRootStateManager.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "nsCSSColorUtils.h"
#include "nsCSSRendering.h"
#include "nsCSSRenderingGradients.h"
#include "nsClassHashtable.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsExpirationTracker.h"
#include "nsIScriptError.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsStyleConsts.h"
#include "nsStyleStruct.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;

#define MAX_COMPOSITE_BORDER_WIDTH LayoutDeviceIntCoord(10000)

/**
 * nsCSSRendering::PaintBorder
 * nsCSSRendering::PaintOutline
 *   -> DrawBorders
 *
 * DrawBorders
 *   -> Ability to use specialized approach?
 *      |- Draw using specialized function
 *   |- separate corners?
 *   |- dashed side mask
 *   |
 *   -> can border be drawn in 1 pass? (e.g., solid border same color all
 * around)
 *      |- DrawBorderSides with all 4 sides
 *   -> more than 1 pass?
 *      |- for each corner
 *         |- clip to DoCornerClipSubPath
 *         |- for each side adjacent to corner
 *            |- clip to GetSideClipSubPath
 *            |- DrawBorderSides with one side
 *      |- for each side
 *         |- GetSideClipWithoutCornersRect
 *         |- DrawDashedOrDottedSide || DrawBorderSides with one side
 */

static void ComputeBorderCornerDimensions(const Margin& aBorderWidths,
                                          const RectCornerRadii& aRadii,
                                          RectCornerRadii* aDimsResult);

// given a side index, get the previous and next side index
#define NEXT_SIDE(_s) mozilla::Side(((_s) + 1) & 3)
#define PREV_SIDE(_s) mozilla::Side(((_s) + 3) & 3)

// given a corner index, get the previous and next corner index
#define NEXT_CORNER(_s) Corner(((_s) + 1) & 3)
#define PREV_CORNER(_s) Corner(((_s) + 3) & 3)

// from the given base color and the background color, turn
// color into a color for the given border pattern style
static sRGBColor MakeBorderColor(nscolor aColor,
                                 BorderColorStyle aBorderColorStyle);

// Given a line index (an index starting from the outside of the
// border going inwards) and an array of line styles, calculate the
// color that that stripe of the border should be rendered in.
static sRGBColor ComputeColorForLine(uint32_t aLineIndex,
                                     const BorderColorStyle* aBorderColorStyle,
                                     uint32_t aBorderColorStyleCount,
                                     nscolor aBorderColor);

static bool IsZeroSize(const Size& sz) {
  return sz.width == 0.0 || sz.height == 0.0;
}

/* static */
bool nsCSSBorderRenderer::AllCornersZeroSize(const RectCornerRadii& corners) {
  return corners.IsEmpty();
}

static mozilla::Side GetHorizontalSide(Corner aCorner) {
  return (aCorner == C_TL || aCorner == C_TR) ? eSideTop : eSideBottom;
}

static mozilla::Side GetVerticalSide(Corner aCorner) {
  return (aCorner == C_TL || aCorner == C_BL) ? eSideLeft : eSideRight;
}

static Corner GetCWCorner(mozilla::Side aSide) {
  return Corner(NEXT_SIDE(aSide));
}

static Corner GetCCWCorner(mozilla::Side aSide) { return Corner(aSide); }

static bool IsSingleSide(mozilla::SideBits aSides) {
  return aSides == SideBits::eTop || aSides == SideBits::eRight ||
         aSides == SideBits::eBottom || aSides == SideBits::eLeft;
}

static bool IsHorizontalSide(mozilla::Side aSide) {
  return aSide == eSideTop || aSide == eSideBottom;
}

typedef enum {
  // Normal solid square corner.  Will be rectangular, the size of the
  // adjacent sides.  If the corner has a border radius, the corner
  // will always be solid, since we don't do dotted/dashed etc.
  CORNER_NORMAL,

  // Paint the corner in whatever style is not dotted/dashed of the
  // adjacent corners.
  CORNER_SOLID,

  // Paint the corner as a dot, the size of the bigger of the adjacent
  // sides.
  CORNER_DOT
} CornerStyle;

nsCSSBorderRenderer::nsCSSBorderRenderer(
    nsPresContext* aPresContext, DrawTarget* aDrawTarget,
    const Rect& aDirtyRect, Rect& aOuterRect,
    const StyleBorderStyle* aBorderStyles, const Margin& aBorderWidths,
    RectCornerRadii& aBorderRadii, const Margin& aBorderInset,
    const nscolor* aBorderColors, bool aBackfaceIsVisible,
    const Maybe<Rect>& aClipRect)
    : mPresContext(aPresContext),
      mDrawTarget(aDrawTarget),
      mDirtyRect(aDirtyRect),
      mOuterRect(aOuterRect),
      mBorderWidths(aBorderWidths),
      mBorderRadii(aBorderRadii),
      mBorderInset(aBorderInset),
      mBackfaceIsVisible(aBackfaceIsVisible),
      mLocalClip(aClipRect) {
  PodCopy(mBorderStyles, aBorderStyles, 4);
  PodCopy(mBorderColors, aBorderColors, 4);
  mInnerRect = mOuterRect;
  mInnerRect.Deflate(Margin(
      mBorderStyles[0] != StyleBorderStyle::None ? mBorderWidths.top.value : 0,
      mBorderStyles[1] != StyleBorderStyle::None ? mBorderWidths.right.value
                                                 : 0,
      mBorderStyles[2] != StyleBorderStyle::None ? mBorderWidths.bottom.value
                                                 : 0,
      mBorderStyles[3] != StyleBorderStyle::None ? mBorderWidths.left.value
                                                 : 0));

  ComputeBorderCornerDimensions(mBorderWidths, mBorderRadii,
                                &mBorderCornerDimensions);

  mOneUnitBorder = mBorderWidths.IsAll(1.0f);
  mNoBorderRadius = AllCornersZeroSize(mBorderRadii);
  mAllBordersSameStyle = AreBorderSideFinalStylesSame(SideBits::eAll);
  mAllBordersSameWidth = mBorderWidths.IsAllEqual();
  mAvoidStroke = false;
}

/* static */
void nsCSSBorderRenderer::ComputeInnerRadii(const RectCornerRadii& aRadii,
                                            const Margin& aBorderSizes,
                                            RectCornerRadii* aInnerRadiiRet) {
  *aInnerRadiiRet = aRadii;
  aInnerRadiiRet->AdjustInwards(aBorderSizes);
}

/* static */
void nsCSSBorderRenderer::ComputeOuterRadii(const RectCornerRadii& aRadii,
                                            const Margin& aBorderSizes,
                                            RectCornerRadii* aOuterRadiiRet) {
  *aOuterRadiiRet = aRadii;
  aOuterRadiiRet->AdjustOutwards(aBorderSizes);
}

/*static*/ void ComputeBorderCornerDimensions(const Margin& aBorderWidths,
                                              const RectCornerRadii& aRadii,
                                              RectCornerRadii* aDimsRet) {
  Float leftWidth = aBorderWidths.left;
  Float topWidth = aBorderWidths.top;
  Float rightWidth = aBorderWidths.right;
  Float bottomWidth = aBorderWidths.bottom;

  if (nsCSSBorderRenderer::AllCornersZeroSize(aRadii)) {
    // These will always be in pixel units from CSS
    (*aDimsRet)[C_TL] = Size(leftWidth, topWidth);
    (*aDimsRet)[C_TR] = Size(rightWidth, topWidth);
    (*aDimsRet)[C_BR] = Size(rightWidth, bottomWidth);
    (*aDimsRet)[C_BL] = Size(leftWidth, bottomWidth);
  } else {
    // Always round up to whole pixels for the corners; it's safe to
    // make the corners bigger than necessary, and this way we ensure
    // that we avoid seams.
    (*aDimsRet)[C_TL] = Size(ceil(std::max(leftWidth, aRadii[C_TL].width)),
                             ceil(std::max(topWidth, aRadii[C_TL].height)));
    (*aDimsRet)[C_TR] = Size(ceil(std::max(rightWidth, aRadii[C_TR].width)),
                             ceil(std::max(topWidth, aRadii[C_TR].height)));
    (*aDimsRet)[C_BR] = Size(ceil(std::max(rightWidth, aRadii[C_BR].width)),
                             ceil(std::max(bottomWidth, aRadii[C_BR].height)));
    (*aDimsRet)[C_BL] = Size(ceil(std::max(leftWidth, aRadii[C_BL].width)),
                             ceil(std::max(bottomWidth, aRadii[C_BL].height)));
  }
}

bool nsCSSBorderRenderer::AreBorderSideFinalStylesSame(
    mozilla::SideBits aSides) {
  NS_ASSERTION(aSides != SideBits::eNone &&
                   (aSides & ~SideBits::eAll) == SideBits::eNone,
               "AreBorderSidesSame: invalid whichSides!");

  /* First check if the specified styles and colors are the same for all sides
   */
  int firstStyle = 0;
  for (const auto i : mozilla::AllPhysicalSides()) {
    if (firstStyle == i) {
      if ((static_cast<mozilla::SideBits>(1 << i) & aSides) ==
          SideBits::eNone) {
        firstStyle++;
      }
      continue;
    }

    if ((static_cast<mozilla::SideBits>(1 << i) & aSides) == SideBits::eNone) {
      continue;
    }

    if (mBorderStyles[firstStyle] != mBorderStyles[i] ||
        mBorderColors[firstStyle] != mBorderColors[i]) {
      return false;
    }
  }

  /* Then if it's one of the two-tone styles and we're not
   * just comparing the TL or BR sides */
  switch (mBorderStyles[firstStyle]) {
    case StyleBorderStyle::Groove:
    case StyleBorderStyle::Ridge:
    case StyleBorderStyle::Inset:
    case StyleBorderStyle::Outset:
      return ((aSides & ~(SideBits::eTop | SideBits::eLeft)) ==
                  SideBits::eNone ||
              (aSides & ~(SideBits::eBottom | SideBits::eRight)) ==
                  SideBits::eNone);
    default:
      return true;
  }
}

bool nsCSSBorderRenderer::IsSolidCornerStyle(StyleBorderStyle aStyle,
                                             Corner aCorner) {
  switch (aStyle) {
    case StyleBorderStyle::Solid:
      return true;

    case StyleBorderStyle::Inset:
    case StyleBorderStyle::Outset:
      return (aCorner == eCornerTopLeft || aCorner == eCornerBottomRight);

    case StyleBorderStyle::Groove:
    case StyleBorderStyle::Ridge:
      return mOneUnitBorder &&
             (aCorner == eCornerTopLeft || aCorner == eCornerBottomRight);

    case StyleBorderStyle::Double:
      return mOneUnitBorder;

    default:
      return false;
  }
}

bool nsCSSBorderRenderer::IsCornerMergeable(Corner aCorner) {
  // Corner between dotted borders with same width and small radii is
  // merged into single dot.
  //
  //  widthH / 2.0
  // |<---------->|
  // |            |
  // |radius.width|
  // |<--->|      |
  // |     |      |
  // |    _+------+------------+-----
  // |  /      ###|###         |
  // |/    #######|#######     |
  // +   #########|#########   |
  // |  ##########|##########  |
  // | ###########|########### |
  // | ###########|########### |
  // |############|############|
  // +------------+############|
  // |#########################|
  // | ####################### |
  // | ####################### |
  // |  #####################  |
  // |   ###################   |
  // |     ###############     |
  // |         #######         |
  // +-------------------------+----
  // |                         |
  // |                         |
  mozilla::Side sideH(GetHorizontalSide(aCorner));
  mozilla::Side sideV(GetVerticalSide(aCorner));
  StyleBorderStyle styleH = mBorderStyles[sideH];
  StyleBorderStyle styleV = mBorderStyles[sideV];
  if (styleH != styleV || styleH != StyleBorderStyle::Dotted) {
    return false;
  }

  Float widthH = mBorderWidths.Side(sideH);
  Float widthV = mBorderWidths.Side(sideV);
  if (widthH != widthV) {
    return false;
  }

  Size radius = mBorderRadii[aCorner];
  return IsZeroSize(radius) ||
         (radius.width < widthH / 2.0f && radius.height < widthH / 2.0f);
}

BorderColorStyle nsCSSBorderRenderer::BorderColorStyleForSolidCorner(
    StyleBorderStyle aStyle, Corner aCorner) {
  // note that this function assumes that the corner is already solid,
  // as per the earlier function
  switch (aStyle) {
    case StyleBorderStyle::Solid:
    case StyleBorderStyle::Double:
      return BorderColorStyleSolid;

    case StyleBorderStyle::Inset:
    case StyleBorderStyle::Groove:
      if (aCorner == eCornerTopLeft) {
        return BorderColorStyleDark;
      } else if (aCorner == eCornerBottomRight) {
        return BorderColorStyleLight;
      }
      break;

    case StyleBorderStyle::Outset:
    case StyleBorderStyle::Ridge:
      if (aCorner == eCornerTopLeft) {
        return BorderColorStyleLight;
      } else if (aCorner == eCornerBottomRight) {
        return BorderColorStyleDark;
      }
      break;
    default:
      return BorderColorStyleNone;
  }

  return BorderColorStyleNone;
}

Rect nsCSSBorderRenderer::GetCornerRect(Corner aCorner) {
  Point offset(0.f, 0.f);

  if (aCorner == C_TR || aCorner == C_BR) {
    offset.x = mOuterRect.Width() - mBorderCornerDimensions[aCorner].width;
  }
  if (aCorner == C_BR || aCorner == C_BL) {
    offset.y = mOuterRect.Height() - mBorderCornerDimensions[aCorner].height;
  }

  return Rect(mOuterRect.TopLeft() + offset, mBorderCornerDimensions[aCorner]);
}

Rect nsCSSBorderRenderer::GetSideClipWithoutCornersRect(mozilla::Side aSide) {
  Point offset(0.f, 0.f);

  // The offset from the outside rect to the start of this side's
  // box.  For the top and bottom sides, the height of the box
  // must be the border height; the x start must take into account
  // the corner size (which may be bigger than the right or left
  // side's width).  The same applies to the right and left sides.
  if (aSide == eSideTop) {
    offset.x = mBorderCornerDimensions[C_TL].width;
  } else if (aSide == eSideRight) {
    offset.x = mOuterRect.Width() - mBorderWidths.right;
    offset.y = mBorderCornerDimensions[C_TR].height;
  } else if (aSide == eSideBottom) {
    offset.x = mBorderCornerDimensions[C_BL].width;
    offset.y = mOuterRect.Height() - mBorderWidths.bottom;
  } else if (aSide == eSideLeft) {
    offset.y = mBorderCornerDimensions[C_TL].height;
  }

  // The sum of the width & height of the corners adjacent to the
  // side.  This relies on the relationship between side indexing and
  // corner indexing; that is, 0 == SIDE_TOP and 0 == CORNER_TOP_LEFT,
  // with both proceeding clockwise.
  Size sideCornerSum = mBorderCornerDimensions[GetCCWCorner(aSide)] +
                       mBorderCornerDimensions[GetCWCorner(aSide)];
  Rect rect(mOuterRect.TopLeft() + offset, mOuterRect.Size() - sideCornerSum);

  if (IsHorizontalSide(aSide)) {
    rect.height = mBorderWidths.Side(aSide);
  } else {
    rect.width = mBorderWidths.Side(aSide);
  }

  return rect;
}

// The side border type and the adjacent border types are
// examined and one of the different types of clipping (listed
// below) is selected.

typedef enum {
  // clip to the trapezoid formed by the corners of the
  // inner and outer rectangles for the given side
  //
  // +---------------
  // |\%%%%%%%%%%%%%%
  // |  \%%%%%%%%%%%%
  // |   \%%%%%%%%%%%
  // |     \%%%%%%%%%
  // |      +--------
  // |      |
  // |      |
  SIDE_CLIP_TRAPEZOID,

  // clip to the trapezoid formed by the outer rectangle
  // corners and the center of the region, making sure
  // that diagonal lines all go directly from the outside
  // corner to the inside corner, but that they then continue on
  // to the middle.
  //
  // This is needed for correctly clipping rounded borders,
  // which might extend past the SIDE_CLIP_TRAPEZOID trap.
  //
  // +-------__--+---
  //  \%%%%_-%%%%%%%%
  //    \+-%%%%%%%%%%
  //    / \%%%%%%%%%%
  //   /   \%%%%%%%%%
  //  |     +%%_-+---
  // |        +%%%%%%
  // |       / \%%%%%
  // +      +    \%%%
  // |      |      +-
  SIDE_CLIP_TRAPEZOID_FULL,

  // clip to the rectangle formed by the given side including corner.
  // This is used by the non-dotted side next to dotted side.
  //
  // +---------------
  // |%%%%%%%%%%%%%%%
  // |%%%%%%%%%%%%%%%
  // |%%%%%%%%%%%%%%%
  // |%%%%%%%%%%%%%%%
  // +------+--------
  // |      |
  // |      |
  SIDE_CLIP_RECTANGLE_CORNER,

  // clip to the rectangle formed by the given side excluding corner.
  // This is used by the dotted side next to non-dotted side.
  //
  // +------+--------
  // |      |%%%%%%%%
  // |      |%%%%%%%%
  // |      |%%%%%%%%
  // |      |%%%%%%%%
  // |      +--------
  // |      |
  // |      |
  SIDE_CLIP_RECTANGLE_NO_CORNER,
} SideClipType;

// Given three points, p0, p1, and midPoint, move p1 further in to the
// rectangle (of which aMidPoint is the center) so that it reaches the
// closer of the horizontal or vertical lines intersecting the midpoint,
// while maintaing the slope of the line.  If p0 and p1 are the same,
// just move p1 to midPoint (since there's no slope to maintain).
// FIXME: Extending only to the midpoint isn't actually sufficient for
// boxes with asymmetric radii.
static void MaybeMoveToMidPoint(Point& aP0, Point& aP1,
                                const Point& aMidPoint) {
  Point ps = aP1 - aP0;

  if (ps.x == 0.0) {
    if (ps.y == 0.0) {
      aP1 = aMidPoint;
    } else {
      aP1.y = aMidPoint.y;
    }
  } else {
    if (ps.y == 0.0) {
      aP1.x = aMidPoint.x;
    } else {
      Float k =
          std::min((aMidPoint.x - aP0.x) / ps.x, (aMidPoint.y - aP0.y) / ps.y);
      aP1 = aP0 + ps * k;
    }
  }
}

already_AddRefed<Path> nsCSSBorderRenderer::GetSideClipSubPath(
    mozilla::Side aSide) {
  // the clip proceeds clockwise from the top left corner;
  // so "start" in each case is the start of the region from that side.
  //
  // the final path will be formed like:
  // s0 ------- e0
  // |         /
  // s1 ----- e1
  //
  // that is, the second point will always be on the inside

  Point start[2];
  Point end[2];

#define IS_DOTTED(_s) ((_s) == StyleBorderStyle::Dotted)
  bool isDotted = IS_DOTTED(mBorderStyles[aSide]);
  bool startIsDotted = IS_DOTTED(mBorderStyles[PREV_SIDE(aSide)]);
  bool endIsDotted = IS_DOTTED(mBorderStyles[NEXT_SIDE(aSide)]);
#undef IS_DOTTED

  SideClipType startType = SIDE_CLIP_TRAPEZOID;
  SideClipType endType = SIDE_CLIP_TRAPEZOID;

  if (!IsZeroSize(mBorderRadii[GetCCWCorner(aSide)])) {
    startType = SIDE_CLIP_TRAPEZOID_FULL;
  } else if (startIsDotted && !isDotted) {
    startType = SIDE_CLIP_RECTANGLE_CORNER;
  } else if (!startIsDotted && isDotted) {
    startType = SIDE_CLIP_RECTANGLE_NO_CORNER;
  }

  if (!IsZeroSize(mBorderRadii[GetCWCorner(aSide)])) {
    endType = SIDE_CLIP_TRAPEZOID_FULL;
  } else if (endIsDotted && !isDotted) {
    endType = SIDE_CLIP_RECTANGLE_CORNER;
  } else if (!endIsDotted && isDotted) {
    endType = SIDE_CLIP_RECTANGLE_NO_CORNER;
  }

  Point midPoint = mInnerRect.Center();

  start[0] = mOuterRect.CCWCorner(aSide);
  start[1] = mInnerRect.CCWCorner(aSide);

  end[0] = mOuterRect.CWCorner(aSide);
  end[1] = mInnerRect.CWCorner(aSide);

  if (startType == SIDE_CLIP_TRAPEZOID_FULL) {
    MaybeMoveToMidPoint(start[0], start[1], midPoint);
  } else if (startType == SIDE_CLIP_RECTANGLE_CORNER) {
    if (IsHorizontalSide(aSide)) {
      start[1] =
          Point(mOuterRect.CCWCorner(aSide).x, mInnerRect.CCWCorner(aSide).y);
    } else {
      start[1] =
          Point(mInnerRect.CCWCorner(aSide).x, mOuterRect.CCWCorner(aSide).y);
    }
  } else if (startType == SIDE_CLIP_RECTANGLE_NO_CORNER) {
    if (IsHorizontalSide(aSide)) {
      start[0] =
          Point(mInnerRect.CCWCorner(aSide).x, mOuterRect.CCWCorner(aSide).y);
    } else {
      start[0] =
          Point(mOuterRect.CCWCorner(aSide).x, mInnerRect.CCWCorner(aSide).y);
    }
  }

  if (endType == SIDE_CLIP_TRAPEZOID_FULL) {
    MaybeMoveToMidPoint(end[0], end[1], midPoint);
  } else if (endType == SIDE_CLIP_RECTANGLE_CORNER) {
    if (IsHorizontalSide(aSide)) {
      end[1] =
          Point(mOuterRect.CWCorner(aSide).x, mInnerRect.CWCorner(aSide).y);
    } else {
      end[1] =
          Point(mInnerRect.CWCorner(aSide).x, mOuterRect.CWCorner(aSide).y);
    }
  } else if (endType == SIDE_CLIP_RECTANGLE_NO_CORNER) {
    if (IsHorizontalSide(aSide)) {
      end[0] =
          Point(mInnerRect.CWCorner(aSide).x, mOuterRect.CWCorner(aSide).y);
    } else {
      end[0] =
          Point(mOuterRect.CWCorner(aSide).x, mInnerRect.CWCorner(aSide).y);
    }
  }

  RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();
  builder->MoveTo(start[0]);
  builder->LineTo(end[0]);
  builder->LineTo(end[1]);
  builder->LineTo(start[1]);
  builder->Close();
  return builder->Finish();
}

Point nsCSSBorderRenderer::GetStraightBorderPoint(mozilla::Side aSide,
                                                  Corner aCorner,
                                                  bool* aIsUnfilled,
                                                  Float aDotOffset) {
  // Calculate the end point of the side for dashed/dotted border, that is also
  // the end point of the corner curve.  The point is specified by aSide and
  // aCorner. (e.g. eSideTop and C_TL means the left end of border-top)
  //
  //
  //  aCorner        aSide
  //         +--------------------
  //         |
  //         |
  //         |         +----------
  //         |    the end point
  //         |
  //         |         +----------
  //         |         |
  //         |         |
  //         |         |
  //
  // The position of the point depends on the border-style, border-width, and
  // border-radius of the side, corner, and the adjacent side beyond the corner,
  // to make those sides (and corner) interact well.
  //
  // If the style of aSide is dotted and the dot at the point should be
  // unfilled, true is stored to *aIsUnfilled, otherwise false is stored.

  const Float signsList[4][2] = {
      {+1.0f, +1.0f}, {-1.0f, +1.0f}, {-1.0f, -1.0f}, {+1.0f, -1.0f}};
  const Float(&signs)[2] = signsList[aCorner];

  *aIsUnfilled = false;

  Point P = mOuterRect.AtCorner(aCorner);
  StyleBorderStyle style = mBorderStyles[aSide];
  Float borderWidth = mBorderWidths.Side(aSide);
  Size dim = mBorderCornerDimensions[aCorner];
  bool isHorizontal = IsHorizontalSide(aSide);
  //
  //    aCorner      aSide
  //           +--------------
  //           |
  //           |   +----------
  //           |   |
  // otherSide |   |
  //           |   |
  mozilla::Side otherSide = ((uint8_t)aSide == (uint8_t)aCorner)
                                ? PREV_SIDE(aSide)
                                : NEXT_SIDE(aSide);
  StyleBorderStyle otherStyle = mBorderStyles[otherSide];
  Float otherBorderWidth = mBorderWidths.Side(otherSide);
  Size radius = mBorderRadii[aCorner];
  if (IsZeroSize(radius)) {
    radius.width = 0.0f;
    radius.height = 0.0f;
  }
  if (style == StyleBorderStyle::Dotted) {
    // Offset the dot's location along the side toward the corner by a
    // multiple of its width.
    if (isHorizontal) {
      P.x -= signs[0] * aDotOffset * borderWidth;
    } else {
      P.y -= signs[1] * aDotOffset * borderWidth;
    }
  }
  if (style == StyleBorderStyle::Dotted &&
      otherStyle == StyleBorderStyle::Dotted) {
    if (borderWidth == otherBorderWidth) {
      if (radius.width < borderWidth / 2.0f &&
          radius.height < borderWidth / 2.0f) {
        // Two dots are merged into one and placed at the corner.
        //
        //  borderWidth / 2.0
        // |<---------->|
        // |            |
        // |radius.width|
        // |<--->|      |
        // |     |      |
        // |    _+------+------------+-----
        // |  /      ###|###         |
        // |/    #######|#######     |
        // +   #########|#########   |
        // |  ##########|##########  |
        // | ###########|########### |
        // | ###########|########### |
        // |############|############|
        // +------------+############|
        // |########### P ###########|
        // | ####################### |
        // | ####################### |
        // |  #####################  |
        // |   ###################   |
        // |     ###############     |
        // |         #######         |
        // +-------------------------+----
        // |                         |
        // |                         |
        P.x += signs[0] * borderWidth / 2.0f;
        P.y += signs[1] * borderWidth / 2.0f;
      } else {
        // Two dots are drawn separately.
        //
        //    borderWidth * 1.5
        //   |<------------>|
        //   |              |
        //   |radius.width  |
        //   |<----->|      |
        //   |       |      |
        //   |    _--+-+----+---
        //   |  _-     |  ##|##
        //   | /       | ###|###
        //   |/        |####|####
        //   |         |####+####
        //   |         |### P ###
        //   +         | ###|###
        //   |         |  ##|##
        //   +---------+----+---
        //   |  #####  |
        //   | ####### |
        //   |#########|
        //   +----+----+
        //   |#########|
        //   | ####### |
        //   |  #####  |
        //   |         |
        //
        // There should be enough gap between 2 dots even if radius.width is
        // small but larger than borderWidth / 2.0.  borderWidth * 1.5 is the
        // value that there's imaginally unfilled dot at the corner.  The
        // unfilled dot may overflow from the outer curve, but filled dots
        // doesn't, so this could be acceptable solution at least for now.
        // We may have to find better model/value.
        //
        //    imaginally unfilled dot at the corner
        //        |
        //        v    +----+---
        //      *****  |  ##|##
        //     ******* | ###|###
        //    *********|####|####
        //    *********|####+####
        //    *********|### P ###
        //     ******* | ###|###
        //      *****  |  ##|##
        //   +---------+----+---
        //   |  #####  |
        //   | ####### |
        //   |#########|
        //   +----+----+
        //   |#########|
        //   | ####### |
        //   |  #####  |
        //   |         |
        Float minimum = borderWidth * 1.5f;
        if (isHorizontal) {
          P.x += signs[0] * std::max(radius.width, minimum);
          P.y += signs[1] * borderWidth / 2.0f;
        } else {
          P.x += signs[0] * borderWidth / 2.0f;
          P.y += signs[1] * std::max(radius.height, minimum);
        }
      }

      return P;
    }

    if (borderWidth < otherBorderWidth) {
      // This side is smaller than other side, other side draws the corner.
      //
      //  otherBorderWidth + borderWidth / 2.0
      // |<---------->|
      // |            |
      // +---------+--+--------
      // |  #####  | *|*  ###
      // | ####### |**|**#####
      // |#########|**+**##+##
      // |####+####|* P *#####
      // |#########| ***  ###
      // | ####### +-----------
      // |  #####  |  ^
      // |         |  |
      // |         | first dot is not filled
      // |         |
      //
      //      radius.width
      // |<----------------->|
      // |                   |
      // |             ___---+-------------
      // |         __--     #|#       ###
      // |       _-        ##|##     #####
      // |     /           ##+##     ##+##
      // |   /             # P #     #####
      // |  |               #|#       ###
      // | |             __--+-------------
      // ||            _-    ^
      // ||           /      |
      // |           /      first dot is filled
      // |          |
      // |          |
      // |  #####  |
      // | ####### |
      // |#########|
      // +----+----+
      // |#########|
      // | ####### |
      // |  #####  |
      Float minimum = otherBorderWidth + borderWidth / 2.0f;
      if (isHorizontal) {
        if (radius.width < minimum) {
          *aIsUnfilled = true;
          P.x += signs[0] * minimum;
        } else {
          P.x += signs[0] * radius.width;
        }
        P.y += signs[1] * borderWidth / 2.0f;
      } else {
        P.x += signs[0] * borderWidth / 2.0f;
        if (radius.height < minimum) {
          *aIsUnfilled = true;
          P.y += signs[1] * minimum;
        } else {
          P.y += signs[1] * radius.height;
        }
      }

      return P;
    }

    // This side is larger than other side, this side draws the corner.
    //
    //  borderWidth / 2.0
    // |<-->|
    // |    |
    // +----+---------------------
    // |  ##|##           #####
    // | ###|###         #######
    // |####|####       #########
    // |####+####       ####+####
    // |### P ###       #########
    // | #######         #######
    // |  #####           #####
    // +-----+---------------------
    // | *** |
    // |*****|
    // |**+**| <-- first dot in other side is not filled
    // |*****|
    // | *** |
    // | ### |
    // |#####|
    // |##+##|
    // |#####|
    // | ### |
    // |     |
    if (isHorizontal) {
      P.x += signs[0] * std::max(radius.width, borderWidth / 2.0f);
      P.y += signs[1] * borderWidth / 2.0f;
    } else {
      P.x += signs[0] * borderWidth / 2.0f;
      P.y += signs[1] * std::max(radius.height, borderWidth / 2.0f);
    }
    return P;
  }

  if (style == StyleBorderStyle::Dotted) {
    // If only this side is dotted, other side draws the corner.
    //
    //  otherBorderWidth + borderWidth / 2.0
    // |<------->|
    // |         |
    // +------+--+--------
    // |##  ##| *|*  ###
    // |##  ##|**|**#####
    // |##  ##|**+**##+##
    // |##  ##|* P *#####
    // |##  ##| ***  ###
    // |##  ##+-----------
    // |##  ##|  ^
    // |##  ##|  |
    // |##  ##| first dot is not filled
    // |##  ##|
    //
    //      radius.width
    // |<----------------->|
    // |                   |
    // |             ___---+-------------
    // |         __--     #|#       ###
    // |       _-        ##|##     #####
    // |     /           ##+##     ##+##
    // |   /             # P #     #####
    // |  |               #|#       ###
    // | |             __--+-------------
    // ||            _-    ^
    // ||          /       |
    // |         /        first dot is filled
    // |        |
    // |       |
    // |      |
    // |      |
    // |      |
    // +------+
    // |##  ##|
    // |##  ##|
    // |##  ##|
    Float minimum = otherBorderWidth + borderWidth / 2.0f;
    if (isHorizontal) {
      if (radius.width < minimum) {
        *aIsUnfilled = true;
        P.x += signs[0] * minimum;
      } else {
        P.x += signs[0] * radius.width;
      }
      P.y += signs[1] * borderWidth / 2.0f;
    } else {
      P.x += signs[0] * borderWidth / 2.0f;
      if (radius.height < minimum) {
        *aIsUnfilled = true;
        P.y += signs[1] * minimum;
      } else {
        P.y += signs[1] * radius.height;
      }
    }
    return P;
  }

  if (otherStyle == StyleBorderStyle::Dotted && IsZeroSize(radius)) {
    // If other side is dotted and radius=0, draw side to the end of corner.
    //
    //   +-------------------------------
    //   |##########          ##########
    // P +##########          ##########
    //   |##########          ##########
    //   +-----+-------------------------
    //   | *** |
    //   |*****|
    //   |**+**| <-- first dot in other side is not filled
    //   |*****|
    //   | *** |
    //   | ### |
    //   |#####|
    //   |##+##|
    //   |#####|
    //   | ### |
    //   |     |
    if (isHorizontal) {
      P.y += signs[1] * borderWidth / 2.0f;
    } else {
      P.x += signs[0] * borderWidth / 2.0f;
    }
    return P;
  }

  // Other cases.
  //
  //  dim.width
  // |<----------------->|
  // |                   |
  // |             ___---+------------------
  // |         __--      |#######        ###
  // |       _-        P +#######        ###
  // |     /             |#######        ###
  // |   /          __---+------------------
  // |  |       __--
  // | |       /
  // ||      /
  // ||     |
  // |     |
  // |    |
  // |   |
  // |   |
  // +-+-+
  // |###|
  // |###|
  // |###|
  // |###|
  // |###|
  // |   |
  // |   |
  if (isHorizontal) {
    P.x += signs[0] * dim.width;
    P.y += signs[1] * borderWidth / 2.0f;
  } else {
    P.x += signs[0] * borderWidth / 2.0f;
    P.y += signs[1] * dim.height;
  }

  return P;
}

void nsCSSBorderRenderer::GetOuterAndInnerBezier(Bezier* aOuterBezier,
                                                 Bezier* aInnerBezier,
                                                 Corner aCorner) {
  // Return bezier control points for outer and inner curve for given corner.
  //
  //               ___---+ outer curve
  //           __--      |
  //         _-          |
  //       /             |
  //     /               |
  //    |                |
  //   |             __--+ inner curve
  //  |            _-
  //  |           /
  // |           /
  // |          |
  // |          |
  // |         |
  // |         |
  // |         |
  // +---------+

  mozilla::Side sideH(GetHorizontalSide(aCorner));
  mozilla::Side sideV(GetVerticalSide(aCorner));

  Size outerCornerSize(ceil(mBorderRadii[aCorner].width),
                       ceil(mBorderRadii[aCorner].height));
  Size innerCornerSize(ceil(std::max(0.0f, mBorderRadii[aCorner].width -
                                               mBorderWidths.Side(sideV))),
                       ceil(std::max(0.0f, mBorderRadii[aCorner].height -
                                               mBorderWidths.Side(sideH))));

  GetBezierPointsForCorner(aOuterBezier, aCorner, mOuterRect.AtCorner(aCorner),
                           outerCornerSize);

  GetBezierPointsForCorner(aInnerBezier, aCorner, mInnerRect.AtCorner(aCorner),
                           innerCornerSize);
}

void nsCSSBorderRenderer::FillSolidBorder(const Rect& aOuterRect,
                                          const Rect& aInnerRect,
                                          const RectCornerRadii& aBorderRadii,
                                          const Margin& aBorderSizes,
                                          SideBits aSides,
                                          const ColorPattern& aColor) {
  // Note that this function is allowed to draw more than just the
  // requested sides.

  // If we have a border radius, do full rounded rectangles
  // and fill, regardless of what sides we're asked to draw.
  if (!AllCornersZeroSize(aBorderRadii)) {
    RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();

    RectCornerRadii innerRadii;
    ComputeInnerRadii(aBorderRadii, aBorderSizes, &innerRadii);

    // do the outer border
    AppendRoundedRectToPath(builder, aOuterRect, aBorderRadii, true);

    // then do the inner border CCW
    AppendRoundedRectToPath(builder, aInnerRect, innerRadii, false);

    RefPtr<Path> path = builder->Finish();

    mDrawTarget->Fill(path, aColor);
    return;
  }

  // If we're asked to draw all sides of an equal-sized border,
  // stroking is fastest.  This is a fairly common path, but partial
  // sides is probably second in the list -- there are a bunch of
  // common border styles, such as inset and outset, that are
  // top-left/bottom-right split.
  if (aSides == SideBits::eAll && aBorderSizes.IsAllEqual() && !mAvoidStroke) {
    Float strokeWidth = aBorderSizes.top.value;
    Rect r(aOuterRect);
    r.Deflate(strokeWidth / 2.f);
    mDrawTarget->StrokeRect(r, aColor, StrokeOptions(strokeWidth));
    return;
  }

  // Otherwise, we have unequal sized borders or we're only
  // drawing some sides; create rectangles for each side
  // and fill them.

  Rect r[4];

  // compute base rects for each side
  if (aSides & SideBits::eTop) {
    r[eSideTop] = Rect(aOuterRect.X(), aOuterRect.Y(), aOuterRect.Width(),
                       aBorderSizes.top);
  }

  if (aSides & SideBits::eBottom) {
    r[eSideBottom] =
        Rect(aOuterRect.X(), aOuterRect.YMost() - aBorderSizes.bottom,
             aOuterRect.Width(), aBorderSizes.bottom);
  }

  if (aSides & SideBits::eLeft) {
    r[eSideLeft] = Rect(aOuterRect.X(), aOuterRect.Y(), aBorderSizes.left,
                        aOuterRect.Height());
  }

  if (aSides & SideBits::eRight) {
    r[eSideRight] =
        Rect(aOuterRect.XMost() - aBorderSizes.right, aOuterRect.Y(),
             aBorderSizes.right, aOuterRect.Height());
  }

  // If two sides meet at a corner that we're rendering, then
  // make sure that we adjust one of the sides to avoid overlap.
  // This is especially important in the case of colors with
  // an alpha channel.

  if ((aSides & (SideBits::eTop | SideBits::eLeft)) ==
      (SideBits::eTop | SideBits::eLeft)) {
    // adjust the left's top down a bit
    r[eSideLeft].y += aBorderSizes.top;
    r[eSideLeft].height -= aBorderSizes.top;
  }

  if ((aSides & (SideBits::eTop | SideBits::eRight)) ==
      (SideBits::eTop | SideBits::eRight)) {
    // adjust the top's left a bit
    r[eSideTop].width -= aBorderSizes.right;
  }

  if ((aSides & (SideBits::eBottom | SideBits::eRight)) ==
      (SideBits::eBottom | SideBits::eRight)) {
    // adjust the right's bottom a bit
    r[eSideRight].height -= aBorderSizes.bottom;
  }

  if ((aSides & (SideBits::eBottom | SideBits::eLeft)) ==
      (SideBits::eBottom | SideBits::eLeft)) {
    // adjust the bottom's left a bit
    r[eSideBottom].x += aBorderSizes.left;
    r[eSideBottom].width -= aBorderSizes.left;
  }

  // Filling these one by one is faster than filling them all at once.
  for (uint32_t i = 0; i < 4; i++) {
    if (aSides & static_cast<mozilla::SideBits>(1 << i)) {
      MaybeSnapToDevicePixels(r[i], *mDrawTarget, true);
      mDrawTarget->FillRect(r[i], aColor);
    }
  }
}

sRGBColor MakeBorderColor(nscolor aColor, BorderColorStyle aBorderColorStyle) {
  nscolor colors[2];
  int k = 0;

  switch (aBorderColorStyle) {
    case BorderColorStyleNone:
      return sRGBColor(0.f, 0.f, 0.f, 0.f);  // transparent black

    case BorderColorStyleLight:
      k = 1;
      [[fallthrough]];
    case BorderColorStyleDark:
      NS_GetSpecial3DColors(colors, aColor);
      return sRGBColor::FromABGR(colors[k]);

    case BorderColorStyleSolid:
    default:
      return sRGBColor::FromABGR(aColor);
  }
}

sRGBColor ComputeColorForLine(uint32_t aLineIndex,
                              const BorderColorStyle* aBorderColorStyle,
                              uint32_t aBorderColorStyleCount,
                              nscolor aBorderColor) {
  NS_ASSERTION(aLineIndex < aBorderColorStyleCount, "Invalid lineIndex given");

  return MakeBorderColor(aBorderColor, aBorderColorStyle[aLineIndex]);
}

void nsCSSBorderRenderer::DrawBorderSides(mozilla::SideBits aSides) {
  if (aSides == SideBits::eNone ||
      (aSides & ~SideBits::eAll) != SideBits::eNone) {
    NS_WARNING("DrawBorderSides: invalid sides!");
    return;
  }

  StyleBorderStyle borderRenderStyle = StyleBorderStyle::None;
  nscolor borderRenderColor;

  uint32_t borderColorStyleCount = 0;
  BorderColorStyle borderColorStyleTopLeft[3], borderColorStyleBottomRight[3];
  BorderColorStyle* borderColorStyle = nullptr;

  for (const auto i : mozilla::AllPhysicalSides()) {
    if ((aSides & static_cast<mozilla::SideBits>(1 << i)) == SideBits::eNone) {
      continue;
    }
    borderRenderStyle = mBorderStyles[i];
    borderRenderColor = mBorderColors[i];
    break;
  }

  if (borderRenderStyle == StyleBorderStyle::None ||
      borderRenderStyle == StyleBorderStyle::Hidden) {
    return;
  }

  if (borderRenderStyle == StyleBorderStyle::Dashed ||
      borderRenderStyle == StyleBorderStyle::Dotted) {
    // Draw each corner separately, with the given side's color.
    if (aSides & SideBits::eTop) {
      DrawDashedOrDottedCorner(eSideTop, C_TL);
    } else if (aSides & SideBits::eLeft) {
      DrawDashedOrDottedCorner(eSideLeft, C_TL);
    }

    if (aSides & SideBits::eTop) {
      DrawDashedOrDottedCorner(eSideTop, C_TR);
    } else if (aSides & SideBits::eRight) {
      DrawDashedOrDottedCorner(eSideRight, C_TR);
    }

    if (aSides & SideBits::eBottom) {
      DrawDashedOrDottedCorner(eSideBottom, C_BL);
    } else if (aSides & SideBits::eLeft) {
      DrawDashedOrDottedCorner(eSideLeft, C_BL);
    }

    if (aSides & SideBits::eBottom) {
      DrawDashedOrDottedCorner(eSideBottom, C_BR);
    } else if (aSides & SideBits::eRight) {
      DrawDashedOrDottedCorner(eSideRight, C_BR);
    }
    return;
  }

  // The borderColorStyle array goes from the outer to the inner style.
  //
  // If the border width is 1, we need to change the borderRenderStyle
  // a bit to make sure that we get the right colors -- e.g. 'ridge'
  // with a 1px border needs to look like solid, not like 'outset'.
  if (mOneUnitBorder && (borderRenderStyle == StyleBorderStyle::Ridge ||
                         borderRenderStyle == StyleBorderStyle::Groove ||
                         borderRenderStyle == StyleBorderStyle::Double)) {
    borderRenderStyle = StyleBorderStyle::Solid;
  }

  switch (borderRenderStyle) {
    case StyleBorderStyle::Solid:
      borderColorStyleTopLeft[0] = BorderColorStyleSolid;

      borderColorStyleBottomRight[0] = BorderColorStyleSolid;

      borderColorStyleCount = 1;
      break;

    case StyleBorderStyle::Groove:
      borderColorStyleTopLeft[0] = BorderColorStyleDark;
      borderColorStyleTopLeft[1] = BorderColorStyleLight;

      borderColorStyleBottomRight[0] = BorderColorStyleLight;
      borderColorStyleBottomRight[1] = BorderColorStyleDark;

      borderColorStyleCount = 2;
      break;

    case StyleBorderStyle::Ridge:
      borderColorStyleTopLeft[0] = BorderColorStyleLight;
      borderColorStyleTopLeft[1] = BorderColorStyleDark;

      borderColorStyleBottomRight[0] = BorderColorStyleDark;
      borderColorStyleBottomRight[1] = BorderColorStyleLight;

      borderColorStyleCount = 2;
      break;

    case StyleBorderStyle::Double:
      borderColorStyleTopLeft[0] = BorderColorStyleSolid;
      borderColorStyleTopLeft[1] = BorderColorStyleNone;
      borderColorStyleTopLeft[2] = BorderColorStyleSolid;

      borderColorStyleBottomRight[0] = BorderColorStyleSolid;
      borderColorStyleBottomRight[1] = BorderColorStyleNone;
      borderColorStyleBottomRight[2] = BorderColorStyleSolid;

      borderColorStyleCount = 3;
      break;

    case StyleBorderStyle::Inset:
      borderColorStyleTopLeft[0] = BorderColorStyleDark;
      borderColorStyleBottomRight[0] = BorderColorStyleLight;

      borderColorStyleCount = 1;
      break;

    case StyleBorderStyle::Outset:
      borderColorStyleTopLeft[0] = BorderColorStyleLight;
      borderColorStyleBottomRight[0] = BorderColorStyleDark;

      borderColorStyleCount = 1;
      break;

    default:
      MOZ_ASSERT_UNREACHABLE("Unhandled border style!!");
      break;
  }

  // The only way to get to here is by having a
  // borderColorStyleCount < 1 or > 3; this should never happen,
  // since -moz-border-colors doesn't get handled here.
  NS_ASSERTION(borderColorStyleCount > 0 && borderColorStyleCount < 4,
               "Non-border-colors case with borderColorStyleCount < 1 or > 3; "
               "what happened?");

  // The caller should never give us anything with a mix
  // of TL/BR if the border style would require a
  // TL/BR split.
  if (aSides & (SideBits::eBottom | SideBits::eRight)) {
    borderColorStyle = borderColorStyleBottomRight;
  } else {
    borderColorStyle = borderColorStyleTopLeft;
  }

  // Distribute the border across the available space.
  Margin borderWidths[3];

  if (borderColorStyleCount == 1) {
    borderWidths[0] = mBorderWidths;
  } else if (borderColorStyleCount == 2) {
    // with 2 color styles, any extra pixel goes to the outside
    for (const auto i : mozilla::AllPhysicalSides()) {
      borderWidths[0].Side(i) = int32_t(mBorderWidths.Side(i)) / 2 +
                                int32_t(mBorderWidths.Side(i)) % 2;
      borderWidths[1].Side(i) = int32_t(mBorderWidths.Side(i)) / 2;
    }
  } else if (borderColorStyleCount == 3) {
    // with 3 color styles, any extra pixel (or lack of extra pixel)
    // goes to the middle
    for (const auto i : mozilla::AllPhysicalSides()) {
      if (mBorderWidths.Side(i) == 1.0) {
        borderWidths[0].Side(i) = 1.f;
        borderWidths[1].Side(i) = borderWidths[2].Side(i) = 0.f;
      } else {
        int32_t rest = int32_t(mBorderWidths.Side(i)) % 3;
        borderWidths[0].Side(i) = borderWidths[2].Side(i) =
            borderWidths[1].Side(i) =
                (int32_t(mBorderWidths.Side(i)) - rest) / 3;

        if (rest == 1) {
          borderWidths[1].Side(i) += 1.f;
        } else if (rest == 2) {
          borderWidths[0].Side(i) += 1.f;
          borderWidths[2].Side(i) += 1.f;
        }
      }
    }
  }

  // make a copy that we can modify
  RectCornerRadii radii = mBorderRadii;

  Rect soRect(mOuterRect);
  Rect siRect(mOuterRect);

  // If adjacent side is dotted and radius=0, draw side to the end of corner.
  //
  // +--------------------------------
  // |################################
  // |
  // |################################
  // +-----+--------------------------
  // |     |
  // |     |
  // |     |
  // |     |
  // |     |
  // | ### |
  // |#####|
  // |#####|
  // |#####|
  // | ### |
  // |     |
  bool noMarginTop = false;
  bool noMarginRight = false;
  bool noMarginBottom = false;
  bool noMarginLeft = false;

  // If there is at least one dotted side, every side is rendered separately.
  if (IsSingleSide(aSides)) {
    if (aSides == SideBits::eTop) {
      if (mBorderStyles[eSideRight] == StyleBorderStyle::Dotted &&
          IsZeroSize(mBorderRadii[C_TR])) {
        noMarginRight = true;
      }
      if (mBorderStyles[eSideLeft] == StyleBorderStyle::Dotted &&
          IsZeroSize(mBorderRadii[C_TL])) {
        noMarginLeft = true;
      }
    } else if (aSides == SideBits::eRight) {
      if (mBorderStyles[eSideTop] == StyleBorderStyle::Dotted &&
          IsZeroSize(mBorderRadii[C_TR])) {
        noMarginTop = true;
      }
      if (mBorderStyles[eSideBottom] == StyleBorderStyle::Dotted &&
          IsZeroSize(mBorderRadii[C_BR])) {
        noMarginBottom = true;
      }
    } else if (aSides == SideBits::eBottom) {
      if (mBorderStyles[eSideRight] == StyleBorderStyle::Dotted &&
          IsZeroSize(mBorderRadii[C_BR])) {
        noMarginRight = true;
      }
      if (mBorderStyles[eSideLeft] == StyleBorderStyle::Dotted &&
          IsZeroSize(mBorderRadii[C_BL])) {
        noMarginLeft = true;
      }
    } else {
      if (mBorderStyles[eSideTop] == StyleBorderStyle::Dotted &&
          IsZeroSize(mBorderRadii[C_TL])) {
        noMarginTop = true;
      }
      if (mBorderStyles[eSideBottom] == StyleBorderStyle::Dotted &&
          IsZeroSize(mBorderRadii[C_BL])) {
        noMarginBottom = true;
      }
    }
  }

  for (unsigned int i = 0; i < borderColorStyleCount; i++) {
    // walk siRect inwards at the start of the loop to get the
    // correct inner rect.
    //
    // If noMarginTop is false:
    //   --------------------+
    //                      /|
    //                     / |
    //                    L  |
    //   ----------------+   |
    //                   |   |
    //                   |   |
    //
    // If noMarginTop is true:
    //   ----------------+<--+
    //                   |   |
    //                   |   |
    //                   |   |
    //                   |   |
    //                   |   |
    //                   |   |
    siRect.Deflate(Margin(noMarginTop ? 0 : borderWidths[i].top.value,
                          noMarginRight ? 0 : borderWidths[i].right.value,
                          noMarginBottom ? 0 : borderWidths[i].bottom.value,
                          noMarginLeft ? 0 : borderWidths[i].left.value));

    if (borderColorStyle[i] != BorderColorStyleNone) {
      sRGBColor c = ComputeColorForLine(
          i, borderColorStyle, borderColorStyleCount, borderRenderColor);
      ColorPattern color(ToDeviceColor(c));

      FillSolidBorder(soRect, siRect, radii, borderWidths[i], aSides, color);
    }

    ComputeInnerRadii(radii, borderWidths[i], &radii);

    // And now soRect is the same as siRect, for the next line in.
    soRect = siRect;
  }
}

void nsCSSBorderRenderer::SetupDashedOptions(StrokeOptions* aStrokeOptions,
                                             Float aDash[2],
                                             mozilla::Side aSide,
                                             Float aBorderLength,
                                             bool isCorner) {
  MOZ_ASSERT(mBorderStyles[aSide] == StyleBorderStyle::Dashed ||
                 mBorderStyles[aSide] == StyleBorderStyle::Dotted,
             "Style should be dashed or dotted.");

  StyleBorderStyle style = mBorderStyles[aSide];
  Float borderWidth = mBorderWidths.Side(aSide);

  // Dashed line starts and ends with half segment in most case.
  //
  // __--+---+---+---+---+---+---+---+---+--__
  //     |###|   |   |###|###|   |   |###|
  //     |###|   |   |###|###|   |   |###|
  //     |###|   |   |###|###|   |   |###|
  // __--+---+---+---+---+---+---+---+---+--__
  //
  // If radius=0 and other side is either dotted or 0-width, it starts or ends
  // with full segment.
  //
  // +---+---+---+---+---+---+---+---+---+---+
  // |###|###|   |   |###|###|   |   |###|###|
  // |###|###|   |   |###|###|   |   |###|###|
  // |###|###|   |   |###|###|   |   |###|###|
  // +---++--+---+---+---+---+---+---+--++---+
  // |    |                             |    |
  // |    |                             |    |
  // |    |                             |    |
  // |    |                             |    |
  // | ## |                             | ## |
  // |####|                             |####|
  // |####|                             |####|
  // | ## |                             | ## |
  // |    |                             |    |
  bool fullStart = false, fullEnd = false;
  Float halfDash;
  if (style == StyleBorderStyle::Dashed) {
    // If either end of the side is not connecting onto a corner then we want a
    // full dash at that end.
    //
    // Note that in the case that a corner is empty, either the adjacent side
    // has zero width, or else DrawBorders() set the corner to be empty
    // (it does that if the adjacent side has zero length and the border widths
    // of this and the adjacent sides are thin enough that the corner will be
    // insignificantly small).

    if (mBorderRadii[GetCCWCorner(aSide)].IsEmpty() &&
        (mBorderCornerDimensions[GetCCWCorner(aSide)].IsEmpty() ||
         mBorderStyles[PREV_SIDE(aSide)] == StyleBorderStyle::Dotted ||
         // XXX why this <=1 check?
         borderWidth <= 1.0f)) {
      fullStart = true;
    }

    if (mBorderRadii[GetCWCorner(aSide)].IsEmpty() &&
        (mBorderCornerDimensions[GetCWCorner(aSide)].IsEmpty() ||
         mBorderStyles[NEXT_SIDE(aSide)] == StyleBorderStyle::Dotted)) {
      fullEnd = true;
    }

    halfDash = borderWidth * DOT_LENGTH * DASH_LENGTH / 2.0f;
  } else {
    halfDash = borderWidth * DOT_LENGTH / 2.0f;
  }

  if (style == StyleBorderStyle::Dashed && aBorderLength > 0.0f) {
    // The number of half segments, with maximum dash length.
    int32_t count = floor(aBorderLength / halfDash);
    Float minHalfDash = borderWidth * DOT_LENGTH / 2.0f;

    if (fullStart && fullEnd) {
      // count should be 4n + 2
      //
      //   1 +       4       +        4      + 1
      //
      // |   |               |               |   |
      // +---+---+---+---+---+---+---+---+---+---+
      // |###|###|   |   |###|###|   |   |###|###|
      // |###|###|   |   |###|###|   |   |###|###|
      // |###|###|   |   |###|###|   |   |###|###|
      // +---+---+---+---+---+---+---+---+---+---+

      // If border is too short, draw solid line.
      if (aBorderLength < 6.0f * minHalfDash) {
        return;
      }

      if (count % 4 == 0) {
        count += 2;
      } else if (count % 4 == 1) {
        count += 1;
      } else if (count % 4 == 3) {
        count += 3;
      }
    } else if (fullStart || fullEnd) {
      // count should be 4n + 1
      //
      //   1 +       4       +        4
      //
      // |   |               |               |
      // +---+---+---+---+---+---+---+---+---+
      // |###|###|   |   |###|###|   |   |###|
      // |###|###|   |   |###|###|   |   |###|
      // |###|###|   |   |###|###|   |   |###|
      // +---+---+---+---+---+---+---+---+---+
      //
      //         4       +        4      + 1
      //
      // |               |               |   |
      // +---+---+---+---+---+---+---+---+---+
      // |###|   |   |###|###|   |   |###|###|
      // |###|   |   |###|###|   |   |###|###|
      // |###|   |   |###|###|   |   |###|###|
      // +---+---+---+---+---+---+---+---+---+

      // If border is too short, draw solid line.
      if (aBorderLength < 5.0f * minHalfDash) {
        return;
      }

      if (count % 4 == 0) {
        count += 1;
      } else if (count % 4 == 2) {
        count += 3;
      } else if (count % 4 == 3) {
        count += 2;
      }
    } else {
      // count should be 4n
      //
      //         4       +        4
      //
      // |               |               |
      // +---+---+---+---+---+---+---+---+
      // |###|   |   |###|###|   |   |###|
      // |###|   |   |###|###|   |   |###|
      // |###|   |   |###|###|   |   |###|
      // +---+---+---+---+---+---+---+---+

      // If border is too short, draw solid line.
      if (aBorderLength < 4.0f * minHalfDash) {
        return;
      }

      if (count % 4 == 1) {
        count += 3;
      } else if (count % 4 == 2) {
        count += 2;
      } else if (count % 4 == 3) {
        count += 1;
      }
    }
    halfDash = aBorderLength / count;
  }

  Float fullDash = halfDash * 2.0f;

  aDash[0] = fullDash;
  aDash[1] = fullDash;

  if (style == StyleBorderStyle::Dashed && fullDash > 1.0f) {
    if (!fullStart) {
      // Draw half segments on both ends.
      aStrokeOptions->mDashOffset = halfDash;
    }
  } else if (style != StyleBorderStyle::Dotted && isCorner) {
    // If side ends with filled full segment, corner should start with unfilled
    // full segment. Not needed for dotted corners, as they overlap one dot with
    // the side's end.
    //
    //     corner            side
    //   ------------>|<---------------------------
    //                |
    //          __+---+---+---+---+---+---+---+---+
    //       _+-  |   |###|###|   |   |###|###|   |
    //     /##|   |   |###|###|   |   |###|###|   |
    //    +####|   |  |###|###|   |   |###|###|   |
    //   /#\####| _+--+---+---+---+---+---+---+---+
    //  |####\##+-
    //  |#####+-
    //  +--###/
    //  |  --+
    aStrokeOptions->mDashOffset = fullDash;
  }

  aStrokeOptions->mDashPattern = aDash;
  aStrokeOptions->mDashLength = 2;

  PrintAsFormatString("dash: %f %f\n", aDash[0], aDash[1]);
}

static Float GetBorderLength(mozilla::Side aSide, const Point& aStart,
                             const Point& aEnd) {
  if (aSide == eSideTop) {
    return aEnd.x - aStart.x;
  }
  if (aSide == eSideRight) {
    return aEnd.y - aStart.y;
  }
  if (aSide == eSideBottom) {
    return aStart.x - aEnd.x;
  }
  return aStart.y - aEnd.y;
}

void nsCSSBorderRenderer::DrawDashedOrDottedSide(mozilla::Side aSide) {
  // Draw dashed/dotted side with following approach.
  //
  // dashed side
  //   Draw dashed line along the side, with appropriate dash length and gap
  //   to make the side symmetric as far as possible.  Dash length equals to
  //   the gap, and the ratio of the dash length to border-width is the maximum
  //   value in in [1, 3] range.
  //   In most case, line ends with half segment, to joint with corner easily.
  //   If adjacent side is dotted or 0px and border-radius for the corner
  //   between them is 0, the line ends with full segment.
  //   (see comment for GetStraightBorderPoint for more detail)
  //
  // dotted side
  //   If border-width <= 2.0, draw 1:1 dashed line.
  //   Otherwise, draw circles along the side, with appropriate gap that makes
  //   the side symmetric as far as possible.  The ratio of the gap to
  //   border-width is the maximum value in [0.5, 1] range in most case.
  //   if the side is too short and there's only 2 dots, it can be more smaller.
  //   If there's no space to place 2 dots at the side, draw single dot at the
  //   middle of the side.
  //   In most case, line ends with filled dot, to joint with corner easily,
  //   If adjacent side is dotted with larger border-width, or other style,
  //   the line ends with unfilled dot.
  //   (see comment for GetStraightBorderPoint for more detail)

  NS_ASSERTION(mBorderStyles[aSide] == StyleBorderStyle::Dashed ||
                   mBorderStyles[aSide] == StyleBorderStyle::Dotted,
               "Style should be dashed or dotted.");

  Float borderWidth = mBorderWidths.Side(aSide);
  if (borderWidth == 0.0f) {
    return;
  }

  if (mBorderStyles[aSide] == StyleBorderStyle::Dotted && borderWidth > 2.0f) {
    DrawDottedSideSlow(aSide);
    return;
  }

  nscolor borderColor = mBorderColors[aSide];
  bool ignored;
  // Get the start and end points of the side, ensuring that any dot origins get
  // pushed outward to account for stroking.
  Point start =
      GetStraightBorderPoint(aSide, GetCCWCorner(aSide), &ignored, 0.5f);
  Point end = GetStraightBorderPoint(aSide, GetCWCorner(aSide), &ignored, 0.5f);
  if (borderWidth < 2.0f) {
    // Round start to draw dot on each pixel.
    if (IsHorizontalSide(aSide)) {
      start.x = round(start.x);
    } else {
      start.y = round(start.y);
    }
  }

  Float borderLength = GetBorderLength(aSide, start, end);
  if (borderLength < 0.0f) {
    return;
  }

  StrokeOptions strokeOptions(borderWidth);
  Float dash[2];
  SetupDashedOptions(&strokeOptions, dash, aSide, borderLength, false);

  // For dotted sides that can merge with their prior dotted sides, advance the
  // dash offset to measure the distance around the combined path. This prevents
  // two dots from bunching together at a corner.
  mozilla::Side mergeSide = aSide;
  while (IsCornerMergeable(GetCCWCorner(mergeSide))) {
    mergeSide = PREV_SIDE(mergeSide);
    // If we looped all the way around, measure starting at the top side, since
    // we need to pick a fixed location to start measuring distance from still.
    if (mergeSide == aSide) {
      mergeSide = eSideTop;
      break;
    }
  }
  while (mergeSide != aSide) {
    // Measure the length of the merged side starting from a possibly
    // unmergeable corner up to the merged corner. A merged corner effectively
    // has no border radius, so we can just use the cheaper AtCorner to find the
    // end point.
    Float mergeLength =
        GetBorderLength(mergeSide,
                        GetStraightBorderPoint(
                            mergeSide, GetCCWCorner(mergeSide), &ignored, 0.5f),
                        mOuterRect.AtCorner(GetCWCorner(mergeSide)));
    // Add in the merged side length. Also offset the dash progress by an extra
    // dot's width to avoid drawing a dot that would overdraw where the merged
    // side would have ended in a gap, i.e. O_O_
    //                                    O
    strokeOptions.mDashOffset += mergeLength + borderWidth;
    mergeSide = NEXT_SIDE(mergeSide);
  }

  DrawOptions drawOptions;
  if (mBorderStyles[aSide] == StyleBorderStyle::Dotted) {
    drawOptions.mAntialiasMode = AntialiasMode::NONE;
  }

  mDrawTarget->StrokeLine(start, end, ColorPattern(ToDeviceColor(borderColor)),
                          strokeOptions, drawOptions);
}

void nsCSSBorderRenderer::DrawDottedSideSlow(mozilla::Side aSide) {
  // Draw each circles separately for dotted with borderWidth > 2.0.
  // Dashed line with CapStyle::ROUND doesn't render perfect circles.

  NS_ASSERTION(mBorderStyles[aSide] == StyleBorderStyle::Dotted,
               "Style should be dotted.");

  Float borderWidth = mBorderWidths.Side(aSide);
  if (borderWidth == 0.0f) {
    return;
  }

  nscolor borderColor = mBorderColors[aSide];
  bool isStartUnfilled, isEndUnfilled;
  Point start =
      GetStraightBorderPoint(aSide, GetCCWCorner(aSide), &isStartUnfilled);
  Point end = GetStraightBorderPoint(aSide, GetCWCorner(aSide), &isEndUnfilled);
  enum {
    // Corner is not mergeable.
    NO_MERGE,

    // Corner between different colors.
    // Two dots are merged into one, and both side draw half dot.
    MERGE_HALF,

    // Corner between same colors, CCW corner of the side.
    // Two dots are merged into one, and this side draw entire dot.
    //
    // MERGE_ALL               MERGE_NONE
    //   |                       |
    //   v                       v
    // +-----------------------+----+
    // | ##      ##      ##    | ## |
    // |####    ####    ####   |####|
    // |####    ####    ####   |####|
    // | ##      ##      ##    | ## |
    // +----+------------------+    |
    // |    |                  |    |
    // |    |                  |    |
    // |    |                  |    |
    // | ## |                  | ## |
    // |####|                  |####|
    MERGE_ALL,

    // Corner between same colors, CW corner of the side.
    // Two dots are merged into one, and this side doesn't draw dot.
    MERGE_NONE
  } mergeStart = NO_MERGE,
    mergeEnd = NO_MERGE;

  if (IsCornerMergeable(GetCCWCorner(aSide))) {
    if (borderColor == mBorderColors[PREV_SIDE(aSide)]) {
      mergeStart = MERGE_ALL;
    } else {
      mergeStart = MERGE_HALF;
    }
  }

  if (IsCornerMergeable(GetCWCorner(aSide))) {
    if (borderColor == mBorderColors[NEXT_SIDE(aSide)]) {
      mergeEnd = MERGE_NONE;
    } else {
      mergeEnd = MERGE_HALF;
    }
  }

  Float borderLength = GetBorderLength(aSide, start, end);
  if (borderLength < 0.0f) {
    if (isStartUnfilled || isEndUnfilled) {
      return;
    }
    borderLength = 0.0f;
    start = end = (start + end) / 2.0f;
  }

  Float dotWidth = borderWidth * DOT_LENGTH;
  Float radius = borderWidth / 2.0f;
  if (borderLength < dotWidth) {
    // If dots on start and end may overlap, draw a dot at the middle of them.
    //
    //     ___---+-------+---___
    // __--      | ##### |      --__
    //          #|#######|#
    //         ##|#######|##
    //        ###|#######|###
    //        ###+###+###+###
    //         start ## end #
    //         ##|#######|##
    //          #|#######|#
    //           | ##### |
    //       __--+-------+--__
    //     _-                 -_
    //
    // If that circle overflows from outer rect, do not draw it.
    //
    //           +-------+
    //           | ##### |
    //          #|#######|#
    //         ##|#######|##
    //        ###|#######|###
    //        ###|###+###|###
    //        ###|#######|###
    //         ##|#######|##
    //          #|#######|#
    //           | ##### |
    //           +--+-+--+
    //           |  | |  |
    //           |  | |  |
    if (!mOuterRect.Contains(Rect(start.x - radius, start.y - radius,
                                  borderWidth, borderWidth))) {
      return;
    }

    if (isStartUnfilled || isEndUnfilled) {
      return;
    }

    Point P = (start + end) / 2;
    RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();
    builder->MoveTo(Point(P.x + radius, P.y));
    builder->Arc(P, radius, 0.0f, Float(2.0 * M_PI));
    RefPtr<Path> path = builder->Finish();
    mDrawTarget->Fill(path, ColorPattern(ToDeviceColor(borderColor)));
    return;
  }

  if (mergeStart == MERGE_HALF || mergeEnd == MERGE_HALF) {
    // MERGE_HALF
    //               Eo
    //   -------+----+
    //        ##### /
    //       ######/
    //      ######/
    //      ####+
    //      ##/ end
    //       /
    //      /
    //   --+
    //     Ei
    //
    // other (NO_MERGE, MERGE_ALL, MERGE_NONE)
    //               Eo
    //   ------------+
    //        #####  |
    //       ####### |
    //      #########|
    //      ####+####|
    //      ## end ##|
    //       ####### |
    //        #####  |
    //   ------------+
    //               Ei

    Point I(0.0f, 0.0f), J(0.0f, 0.0f);
    if (aSide == eSideTop) {
      I.x = 1.0f;
      J.y = 1.0f;
    } else if (aSide == eSideRight) {
      I.y = 1.0f;
      J.x = -1.0f;
    } else if (aSide == eSideBottom) {
      I.x = -1.0f;
      J.y = -1.0f;
    } else if (aSide == eSideLeft) {
      I.y = -1.0f;
      J.x = 1.0f;
    }

    Point So, Si, Eo, Ei;

    So = (start + (-I + -J) * borderWidth / 2.0f);
    Si = (mergeStart == MERGE_HALF) ? (start + (I + J) * borderWidth / 2.0f)
                                    : (start + (-I + J) * borderWidth / 2.0f);
    Eo = (end + (I - J) * borderWidth / 2.0f);
    Ei = (mergeEnd == MERGE_HALF) ? (end + (-I + J) * borderWidth / 2.0f)
                                  : (end + (I + J) * borderWidth / 2.0f);

    RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();
    builder->MoveTo(So);
    builder->LineTo(Eo);
    builder->LineTo(Ei);
    builder->LineTo(Si);
    builder->Close();
    RefPtr<Path> path = builder->Finish();

    mDrawTarget->PushClip(path);
  }

  size_t count = round(borderLength / dotWidth);
  if (isStartUnfilled == isEndUnfilled) {
    // Split into 2n segments.
    if (count % 2) {
      count++;
    }
  } else {
    // Split into 2n+1 segments.
    if (count % 2 == 0) {
      count++;
    }
  }

  // A: radius == borderWidth / 2.0
  // B: borderLength / count == borderWidth * (1 - overlap)
  //
  //   A      B         B        B        B     A
  // |<-->|<------>|<------>|<------>|<------>|<-->|
  // |    |        |        |        |        |    |
  // +----+--------+--------+--------+--------+----+
  // |  ##|##    **|**    ##|##    **|**    ##|##  |
  // | ###|###  ***|***  ###|###  ***|***  ###|### |
  // |####|####****|****####|####****|****####|####|
  // |####+####****+****####+####****+****####+####|
  // |# start #****|****####|####****|****## end ##|
  // | ###|###  ***|***  ###|###  ***|***  ###|### |
  // |  ##|##    **|**    ##|##    **|**    ##|##  |
  // +----+----+---+--------+--------+---+----+----+
  // |         |                         |         |
  // |         |                         |         |

  // If isStartUnfilled is true, draw dots on 2j+1 points, if not, draw dots on
  // 2j points.
  size_t from = isStartUnfilled ? 1 : 0;

  // If mergeEnd == MERGE_NONE, last dot is drawn by next side.
  size_t to = count;
  if (mergeEnd == MERGE_NONE) {
    if (to > 2) {
      to -= 2;
    } else {
      to = 0;
    }
  }

  Point fromP = (start * (count - from) + end * from) / count;
  Point toP = (start * (count - to) + end * to) / count;
  // Extend dirty rect to avoid clipping pixel for anti-aliasing.
  const Float AA_MARGIN = 2.0f;

  // The following algorithm assumes the border's rect and the dirty rect
  // intersect.
  MOZ_ASSERT(mDirtyRect.Intersects(mOuterRect));

  if (aSide == eSideTop) {
    // Tweak |from| and |to| to fit into |mDirtyRect + radius margin|,
    // to render only paths that may overlap mDirtyRect.
    //
    //                mDirtyRect + radius margin
    //              +--+---------------------+--+
    //              |                           |
    //              |         mDirtyRect        |
    //              +  +---------------------+  +
    // from   ===>  |from                    to |   <===  to
    //    +-----+-----+-----+-----+-----+-----+-----+-----+
    //   ###        |###         ###         ###|        ###
    //  #####       #####       #####       #####       #####
    //  #####       #####       #####       #####       #####
    //  #####       #####       #####       #####       #####
    //   ###        |###         ###         ###|        ###
    //              |  |                     |  |
    //              +  +---------------------+  +
    //              |                           |
    //              |                           |
    //              +--+---------------------+--+

    Float left = mDirtyRect.x - radius - AA_MARGIN;
    if (fromP.x < left) {
      size_t tmp = ceil(count * (left - start.x) / (end.x - start.x));
      if (tmp > from) {
        // We increment by 2, so odd/even should match between before/after.
        if ((tmp & 1) != (from & 1)) {
          from = tmp - 1;
        } else {
          from = tmp;
        }
      }
    }
    Float right = mDirtyRect.x + mDirtyRect.width + radius + AA_MARGIN;
    if (toP.x > right) {
      size_t tmp = floor(count * (right - start.x) / (end.x - start.x));
      if (tmp < to) {
        if ((tmp & 1) != (to & 1)) {
          to = tmp + 1;
        } else {
          to = tmp;
        }
      }
    }
  } else if (aSide == eSideRight) {
    Float top = mDirtyRect.y - radius - AA_MARGIN;
    if (fromP.y < top) {
      size_t tmp = ceil(count * (top - start.y) / (end.y - start.y));
      if (tmp > from) {
        if ((tmp & 1) != (from & 1)) {
          from = tmp - 1;
        } else {
          from = tmp;
        }
      }
    }
    Float bottom = mDirtyRect.y + mDirtyRect.height + radius + AA_MARGIN;
    if (toP.y > bottom) {
      size_t tmp = floor(count * (bottom - start.y) / (end.y - start.y));
      if (tmp < to) {
        if ((tmp & 1) != (to & 1)) {
          to = tmp + 1;
        } else {
          to = tmp;
        }
      }
    }
  } else if (aSide == eSideBottom) {
    Float right = mDirtyRect.x + mDirtyRect.width + radius + AA_MARGIN;
    if (fromP.x > right) {
      size_t tmp = ceil(count * (right - start.x) / (end.x - start.x));
      if (tmp > from) {
        if ((tmp & 1) != (from & 1)) {
          from = tmp - 1;
        } else {
          from = tmp;
        }
      }
    }
    Float left = mDirtyRect.x - radius - AA_MARGIN;
    if (toP.x < left) {
      size_t tmp = floor(count * (left - start.x) / (end.x - start.x));
      if (tmp < to) {
        if ((tmp & 1) != (to & 1)) {
          to = tmp + 1;
        } else {
          to = tmp;
        }
      }
    }
  } else if (aSide == eSideLeft) {
    Float bottom = mDirtyRect.y + mDirtyRect.height + radius + AA_MARGIN;
    if (fromP.y > bottom) {
      size_t tmp = ceil(count * (bottom - start.y) / (end.y - start.y));
      if (tmp > from) {
        if ((tmp & 1) != (from & 1)) {
          from = tmp - 1;
        } else {
          from = tmp;
        }
      }
    }
    Float top = mDirtyRect.y - radius - AA_MARGIN;
    if (toP.y < top) {
      size_t tmp = floor(count * (top - start.y) / (end.y - start.y));
      if (tmp < to) {
        if ((tmp & 1) != (to & 1)) {
          to = tmp + 1;
        } else {
          to = tmp;
        }
      }
    }
  }

  RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();
  size_t segmentCount = 0;
  for (size_t i = from; i <= to; i += 2) {
    if (segmentCount > BORDER_SEGMENT_COUNT_MAX) {
      RefPtr<Path> path = builder->Finish();
      mDrawTarget->Fill(path, ColorPattern(ToDeviceColor(borderColor)));
      builder = mDrawTarget->CreatePathBuilder();
      segmentCount = 0;
    }

    Point P = (start * (count - i) + end * i) / count;
    builder->MoveTo(Point(P.x + radius, P.y));
    builder->Arc(P, radius, 0.0f, Float(2.0 * M_PI));
    segmentCount++;
  }
  RefPtr<Path> path = builder->Finish();
  mDrawTarget->Fill(path, ColorPattern(ToDeviceColor(borderColor)));

  if (mergeStart == MERGE_HALF || mergeEnd == MERGE_HALF) {
    mDrawTarget->PopClip();
  }
}

void nsCSSBorderRenderer::DrawDashedOrDottedCorner(mozilla::Side aSide,
                                                   Corner aCorner) {
  // Draw dashed/dotted corner with following approach.
  //
  // dashed corner
  //   If both side has same border-width and border-width <= 2.0, draw dashed
  //   line along the corner, with appropriate dash length and gap to make the
  //   corner symmetric as far as possible.  Dash length equals to the gap, and
  //   the ratio of the dash length to border-width is the maximum value in in
  //   [1, 3] range.
  //   Otherwise, draw dashed segments along the corner, keeping same dash
  //   length ratio to border-width at that point.
  //   (see DashedCornerFinder.h for more detail)
  //   Line ends with half segments, to joint with both side easily.
  //
  // dotted corner
  //   If both side has same border-width and border-width <= 2.0, draw 1:1
  //   dashed line along the corner.
  //   Otherwise Draw circles along the corner, with appropriate gap that makes
  //   the corner symmetric as far as possible.  The size of the circle may
  //   change along the corner, that is tangent to the outer curver and the
  //   inner curve.  The ratio of the gap to circle diameter is the maximum
  //   value in [0.5, 1] range.
  //   (see DottedCornerFinder.h for more detail)
  //   Corner ends with filled dots but those dots are drawn by
  //   DrawDashedOrDottedSide.  So this may draw no circles if there's no space
  //   between 2 dots at both ends.

  NS_ASSERTION(mBorderStyles[aSide] == StyleBorderStyle::Dashed ||
                   mBorderStyles[aSide] == StyleBorderStyle::Dotted,
               "Style should be dashed or dotted.");

  if (IsCornerMergeable(aCorner)) {
    // DrawDashedOrDottedSide will draw corner.
    return;
  }

  mozilla::Side sideH(GetHorizontalSide(aCorner));
  mozilla::Side sideV(GetVerticalSide(aCorner));
  Float borderWidthH = mBorderWidths.Side(sideH);
  Float borderWidthV = mBorderWidths.Side(sideV);
  if (borderWidthH == 0.0f && borderWidthV == 0.0f) {
    return;
  }

  StyleBorderStyle styleH = mBorderStyles[sideH];
  StyleBorderStyle styleV = mBorderStyles[sideV];

  // Corner between dotted and others with radius=0 is drawn by side.
  if (IsZeroSize(mBorderRadii[aCorner]) &&
      (styleV == StyleBorderStyle::Dotted ||
       styleH == StyleBorderStyle::Dotted)) {
    return;
  }

  Float maxRadius =
      std::max(mBorderRadii[aCorner].width, mBorderRadii[aCorner].height);
  if (maxRadius > BORDER_DOTTED_CORNER_MAX_RADIUS) {
    DrawFallbackSolidCorner(aSide, aCorner);
    return;
  }

  if (borderWidthH != borderWidthV || borderWidthH > 2.0f) {
    StyleBorderStyle style = mBorderStyles[aSide];
    if (style == StyleBorderStyle::Dotted) {
      DrawDottedCornerSlow(aSide, aCorner);
    } else {
      DrawDashedCornerSlow(aSide, aCorner);
    }
    return;
  }

  nscolor borderColor = mBorderColors[aSide];
  Point points[4];
  bool ignored;
  // Get the start and end points of the corner arc, ensuring that any dot
  // origins get pushed backwards towards the edges of the corner rect to
  // account for stroking.
  points[0] = GetStraightBorderPoint(sideH, aCorner, &ignored, -0.5f);
  points[3] = GetStraightBorderPoint(sideV, aCorner, &ignored, -0.5f);
  // Round points to draw dot on each pixel.
  if (borderWidthH < 2.0f) {
    points[0].x = round(points[0].x);
  }
  if (borderWidthV < 2.0f) {
    points[3].y = round(points[3].y);
  }
  points[1] = points[0];
  points[1].x += kKappaFactor * (points[3].x - points[0].x);
  points[2] = points[3];
  points[2].y += kKappaFactor * (points[0].y - points[3].y);

  Float len = GetQuarterEllipticArcLength(fabs(points[0].x - points[3].x),
                                          fabs(points[0].y - points[3].y));

  Float dash[2];
  StrokeOptions strokeOptions(borderWidthH);
  SetupDashedOptions(&strokeOptions, dash, aSide, len, true);

  RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();
  builder->MoveTo(points[0]);
  builder->BezierTo(points[1], points[2], points[3]);
  RefPtr<Path> path = builder->Finish();
  mDrawTarget->Stroke(path, ColorPattern(ToDeviceColor(borderColor)),
                      strokeOptions);
}

void nsCSSBorderRenderer::DrawDottedCornerSlow(mozilla::Side aSide,
                                               Corner aCorner) {
  NS_ASSERTION(mBorderStyles[aSide] == StyleBorderStyle::Dotted,
               "Style should be dotted.");

  mozilla::Side sideH(GetHorizontalSide(aCorner));
  mozilla::Side sideV(GetVerticalSide(aCorner));
  Float R0 = mBorderWidths.Side(sideH) / 2.0f;
  Float Rn = mBorderWidths.Side(sideV) / 2.0f;
  if (R0 == 0.0f && Rn == 0.0f) {
    return;
  }

  nscolor borderColor = mBorderColors[aSide];
  Bezier outerBezier;
  Bezier innerBezier;
  GetOuterAndInnerBezier(&outerBezier, &innerBezier, aCorner);

  bool ignored;
  Point C0 = GetStraightBorderPoint(sideH, aCorner, &ignored);
  Point Cn = GetStraightBorderPoint(sideV, aCorner, &ignored);
  DottedCornerFinder finder(outerBezier, innerBezier, aCorner,
                            mBorderRadii[aCorner].width,
                            mBorderRadii[aCorner].height, C0, R0, Cn, Rn,
                            mBorderCornerDimensions[aCorner]);

  RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();
  size_t segmentCount = 0;
  const Float AA_MARGIN = 2.0f;
  Rect marginedDirtyRect = mDirtyRect;
  marginedDirtyRect.Inflate(std::max(R0, Rn) + AA_MARGIN);
  bool entered = false;
  while (finder.HasMore()) {
    if (segmentCount > BORDER_SEGMENT_COUNT_MAX) {
      RefPtr<Path> path = builder->Finish();
      mDrawTarget->Fill(path, ColorPattern(ToDeviceColor(borderColor)));
      builder = mDrawTarget->CreatePathBuilder();
      segmentCount = 0;
    }

    DottedCornerFinder::Result result = finder.Next();

    if (marginedDirtyRect.Contains(result.C) && result.r > 0) {
      entered = true;
      builder->MoveTo(Point(result.C.x + result.r, result.C.y));
      builder->Arc(result.C, result.r, 0, Float(2.0 * M_PI));
      segmentCount++;
    } else if (entered) {
      break;
    }
  }
  RefPtr<Path> path = builder->Finish();
  mDrawTarget->Fill(path, ColorPattern(ToDeviceColor(borderColor)));
}

static inline bool DashedPathOverlapsRect(Rect& pathRect,
                                          const Rect& marginedDirtyRect,
                                          DashedCornerFinder::Result& result) {
  // Calculate a rect that contains all control points of the |result| path,
  // and check if it intersects with |marginedDirtyRect|.
  pathRect.SetRect(result.outerSectionBezier.mPoints[0].x,
                   result.outerSectionBezier.mPoints[0].y, 0, 0);
  pathRect.ExpandToEnclose(result.outerSectionBezier.mPoints[1]);
  pathRect.ExpandToEnclose(result.outerSectionBezier.mPoints[2]);
  pathRect.ExpandToEnclose(result.outerSectionBezier.mPoints[3]);
  pathRect.ExpandToEnclose(result.innerSectionBezier.mPoints[0]);
  pathRect.ExpandToEnclose(result.innerSectionBezier.mPoints[1]);
  pathRect.ExpandToEnclose(result.innerSectionBezier.mPoints[2]);
  pathRect.ExpandToEnclose(result.innerSectionBezier.mPoints[3]);

  return pathRect.Intersects(marginedDirtyRect);
}

void nsCSSBorderRenderer::DrawDashedCornerSlow(mozilla::Side aSide,
                                               Corner aCorner) {
  NS_ASSERTION(mBorderStyles[aSide] == StyleBorderStyle::Dashed,
               "Style should be dashed.");

  mozilla::Side sideH(GetHorizontalSide(aCorner));
  mozilla::Side sideV(GetVerticalSide(aCorner));
  Float borderWidthH = mBorderWidths.Side(sideH);
  Float borderWidthV = mBorderWidths.Side(sideV);
  if (borderWidthH == 0.0f && borderWidthV == 0.0f) {
    return;
  }

  nscolor borderColor = mBorderColors[aSide];
  Bezier outerBezier;
  Bezier innerBezier;
  GetOuterAndInnerBezier(&outerBezier, &innerBezier, aCorner);

  DashedCornerFinder finder(outerBezier, innerBezier, borderWidthH,
                            borderWidthV, mBorderCornerDimensions[aCorner]);

  RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();
  size_t segmentCount = 0;
  const Float AA_MARGIN = 2.0f;
  Rect marginedDirtyRect = mDirtyRect;
  marginedDirtyRect.Inflate(AA_MARGIN);
  Rect pathRect;
  bool entered = false;
  while (finder.HasMore()) {
    if (segmentCount > BORDER_SEGMENT_COUNT_MAX) {
      RefPtr<Path> path = builder->Finish();
      mDrawTarget->Fill(path, ColorPattern(ToDeviceColor(borderColor)));
      builder = mDrawTarget->CreatePathBuilder();
      segmentCount = 0;
    }

    DashedCornerFinder::Result result = finder.Next();

    if (DashedPathOverlapsRect(pathRect, marginedDirtyRect, result)) {
      entered = true;
      builder->MoveTo(result.outerSectionBezier.mPoints[0]);
      builder->BezierTo(result.outerSectionBezier.mPoints[1],
                        result.outerSectionBezier.mPoints[2],
                        result.outerSectionBezier.mPoints[3]);
      builder->LineTo(result.innerSectionBezier.mPoints[3]);
      builder->BezierTo(result.innerSectionBezier.mPoints[2],
                        result.innerSectionBezier.mPoints[1],
                        result.innerSectionBezier.mPoints[0]);
      builder->LineTo(result.outerSectionBezier.mPoints[0]);
      segmentCount++;
    } else if (entered) {
      break;
    }
  }

  if (outerBezier.mPoints[0].x != innerBezier.mPoints[0].x) {
    // Fill gap before the first section.
    //
    //     outnerPoint[0]
    //         |
    //         v
    //        _+-----------+--
    //      /   \##########|
    //    /      \#########|
    //   +        \########|
    //   |\         \######|
    //   |  \        \#####|
    //   |    \       \####|
    //   |      \       \##|
    //   |        \      \#|
    //   |          \     \|
    //   |            \  _-+--
    //   +--------------+  ^
    //   |              |  |
    //   |              |  innerPoint[0]
    //   |              |
    builder->MoveTo(outerBezier.mPoints[0]);
    builder->LineTo(innerBezier.mPoints[0]);
    builder->LineTo(Point(innerBezier.mPoints[0].x, outerBezier.mPoints[0].y));
    builder->LineTo(outerBezier.mPoints[0]);
  }

  if (outerBezier.mPoints[3].y != innerBezier.mPoints[3].y) {
    // Fill gap after the last section.
    //
    // outnerPoint[3]
    //   |
    //   |
    //   |    _+-----------+--
    //   |  /   \          |
    //   v/      \         |
    //   +        \        |
    //   |\         \      |
    //   |##\        \     |
    //   |####\       \    |
    //   |######\       \  |
    //   |########\      \ |
    //   |##########\     \|
    //   |############\  _-+--
    //   +--------------+<-- innerPoint[3]
    //   |              |
    //   |              |
    //   |              |
    builder->MoveTo(outerBezier.mPoints[3]);
    builder->LineTo(innerBezier.mPoints[3]);
    builder->LineTo(Point(outerBezier.mPoints[3].x, innerBezier.mPoints[3].y));
    builder->LineTo(outerBezier.mPoints[3]);
  }

  RefPtr<Path> path = builder->Finish();
  mDrawTarget->Fill(path, ColorPattern(ToDeviceColor(borderColor)));
}

void nsCSSBorderRenderer::DrawFallbackSolidCorner(mozilla::Side aSide,
                                                  Corner aCorner) {
  // Render too large dashed or dotted corner with solid style, to avoid hangup
  // inside DashedCornerFinder and DottedCornerFinder.

  NS_ASSERTION(mBorderStyles[aSide] == StyleBorderStyle::Dashed ||
                   mBorderStyles[aSide] == StyleBorderStyle::Dotted,
               "Style should be dashed or dotted.");

  nscolor borderColor = mBorderColors[aSide];
  Bezier outerBezier;
  Bezier innerBezier;
  GetOuterAndInnerBezier(&outerBezier, &innerBezier, aCorner);

  RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();

  builder->MoveTo(outerBezier.mPoints[0]);
  builder->BezierTo(outerBezier.mPoints[1], outerBezier.mPoints[2],
                    outerBezier.mPoints[3]);
  builder->LineTo(innerBezier.mPoints[3]);
  builder->BezierTo(innerBezier.mPoints[2], innerBezier.mPoints[1],
                    innerBezier.mPoints[0]);
  builder->LineTo(outerBezier.mPoints[0]);

  RefPtr<Path> path = builder->Finish();
  mDrawTarget->Fill(path, ColorPattern(ToDeviceColor(borderColor)));

  if (!mPresContext->HasWarnedAboutTooLargeDashedOrDottedRadius()) {
    mPresContext->SetHasWarnedAboutTooLargeDashedOrDottedRadius();
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "CSS"_ns, mPresContext->Document(),
        PropertiesFile::CSS_PROPERTIES,
        mBorderStyles[aSide] == StyleBorderStyle::Dashed
            ? "TooLargeDashedRadius"
            : "TooLargeDottedRadius");
  }
}

bool nsCSSBorderRenderer::AllBordersSolid() {
  for (const auto i : mozilla::AllPhysicalSides()) {
    if (mBorderStyles[i] == StyleBorderStyle::Solid ||
        mBorderStyles[i] == StyleBorderStyle::None ||
        mBorderStyles[i] == StyleBorderStyle::Hidden) {
      continue;
    }
    return false;
  }

  return true;
}

static bool IsVisible(StyleBorderStyle aStyle) {
  if (aStyle != StyleBorderStyle::None && aStyle != StyleBorderStyle::Hidden) {
    return true;
  }
  return false;
}

struct twoFloats {
  Float a, b;

  twoFloats operator*(const Size& aSize) const {
    return {a * aSize.width, b * aSize.height};
  }

  twoFloats operator*(Float aScale) const { return {a * aScale, b * aScale}; }

  twoFloats operator+(const Point& aPoint) const {
    return {a + aPoint.x, b + aPoint.y};
  }

  operator Point() const { return Point(a, b); }
};

void nsCSSBorderRenderer::DrawSingleWidthSolidBorder() {
  // Easy enough to deal with.
  Rect rect = mOuterRect;
  rect.Deflate(0.5);

  const twoFloats cornerAdjusts[4] = {
      {+0.5, 0}, {0, +0.5}, {-0.5, 0}, {0, -0.5}};
  for (const auto side : mozilla::AllPhysicalSides()) {
    Point firstCorner = rect.CCWCorner(side) + cornerAdjusts[side];
    Point secondCorner = rect.CWCorner(side) + cornerAdjusts[side];

    ColorPattern color(ToDeviceColor(mBorderColors[side]));

    mDrawTarget->StrokeLine(firstCorner, secondCorner, color);
  }
}

// Intersect a ray from the inner corner to the outer corner
// with the border radius, yielding the intersection point.
static Point IntersectBorderRadius(const Point& aCenter, const Size& aRadius,
                                   const Point& aInnerCorner,
                                   const Point& aCornerDirection) {
  Point toCorner = aCornerDirection;
  // transform to-corner ray to unit-circle space
  toCorner.x /= aRadius.width;
  toCorner.y /= aRadius.height;
  // normalize to-corner ray
  Float cornerDist = toCorner.Length();
  if (cornerDist < 1.0e-6f) {
    return aInnerCorner;
  }
  toCorner = toCorner / cornerDist;
  // ray from inner corner to border radius center
  Point toCenter = aCenter - aInnerCorner;
  // transform to-center ray to unit-circle space
  toCenter.x /= aRadius.width;
  toCenter.y /= aRadius.height;
  // compute offset of intersection with border radius unit circle
  Float offset = toCenter.DotProduct(toCorner);
  // compute discriminant to check for intersections
  Float discrim = 1.0f - toCenter.DotProduct(toCenter) + offset * offset;
  // choose farthest intersection
  offset += sqrtf(std::max(discrim, 0.0f));
  // transform to-corner ray back out of unit-circle space
  toCorner.x *= aRadius.width;
  toCorner.y *= aRadius.height;
  return aInnerCorner + toCorner * offset;
}

// Calculate the split point and split angle for a border radius with
// differing sides.
static inline void SplitBorderRadius(const Point& aCenter, const Size& aRadius,
                                     const Point& aOuterCorner,
                                     const Point& aInnerCorner,
                                     const twoFloats& aCornerMults,
                                     Float aStartAngle, Point& aSplit,
                                     Float& aSplitAngle) {
  Point cornerDir = aOuterCorner - aInnerCorner;
  if (cornerDir.x == cornerDir.y && aRadius.IsSquare()) {
    // optimize 45-degree intersection with circle since we can assume
    // the circle center lies along the intersection edge
    aSplit = aCenter - aCornerMults * (aRadius * Float(1.0f / M_SQRT2));
    aSplitAngle = aStartAngle + 0.5f * M_PI / 2.0f;
  } else {
    aSplit = IntersectBorderRadius(aCenter, aRadius, aInnerCorner, cornerDir);
    aSplitAngle = atan2f((aSplit.y - aCenter.y) / aRadius.height,
                         (aSplit.x - aCenter.x) / aRadius.width);
  }
}

// Compute the size of the skirt needed, given the color alphas
// of each corner side and the slope between them.
static void ComputeCornerSkirtSize(Float aAlpha1, Float aAlpha2, Float aSlopeY,
                                   Float aSlopeX, Float& aSizeResult,
                                   Float& aSlopeResult) {
  // If either side is (almost) invisible or there is no diagonal edge,
  // then don't try to render a skirt.
  if (aAlpha1 < 0.01f || aAlpha2 < 0.01f) {
    return;
  }
  aSlopeX = fabs(aSlopeX);
  aSlopeY = fabs(aSlopeY);
  if (aSlopeX < 1.0e-6f || aSlopeY < 1.0e-6f) {
    return;
  }

  // If first and second color don't match, we need to split the corner in
  // half. The diagonal edges created may not have full pixel coverage given
  // anti-aliasing, so we need to compute a small subpixel skirt edge. This
  // assumes each half has half coverage to start with, and that coverage
  // increases as the skirt is pushed over, with the end result that we want
  // to roughly preserve the alpha value along this edge.
  // Given slope m, alphas a and A, use quadratic formula to solve for S in:
  //   a*(1 - 0.5*(1-S)*(1-mS))*(1 - 0.5*A) + 0.5*A = A
  // yielding:
  //   S = ((1+m) - sqrt((1+m)*(1+m) + 4*m*(1 - A/(a*(1-0.5*A))))) / (2*m)
  // and substitute k = (1+m)/(2*m):
  //   S = k - sqrt(k*k + (1 - A/(a*(1-0.5*A)))/m)
  Float slope = aSlopeY / aSlopeX;
  Float slopeScale = (1.0f + slope) / (2.0f * slope);
  Float discrim = slopeScale * slopeScale +
                  (1 - aAlpha2 / (aAlpha1 * (1.0f - 0.49f * aAlpha2))) / slope;
  if (discrim >= 0) {
    aSizeResult = slopeScale - sqrtf(discrim);
    aSlopeResult = slope;
  }
}

// Draws a border radius with possibly different sides.
// A skirt is drawn underneath the corner intersection to hide possible
// seams when anti-aliased drawing is used.
static void DrawBorderRadius(
    DrawTarget* aDrawTarget, Corner c, const Point& aOuterCorner,
    const Point& aInnerCorner, const twoFloats& aCornerMultPrev,
    const twoFloats& aCornerMultNext, const Size& aCornerDims,
    const Size& aOuterRadius, const Size& aInnerRadius,
    const DeviceColor& aFirstColor, const DeviceColor& aSecondColor,
    Float aSkirtSize, Float aSkirtSlope) {
  // Connect edge to outer arc start point
  Point outerCornerStart = aOuterCorner + aCornerMultPrev * aCornerDims;
  // Connect edge to outer arc end point
  Point outerCornerEnd = aOuterCorner + aCornerMultNext * aCornerDims;
  // Connect edge to inner arc start point
  Point innerCornerStart =
      outerCornerStart + aCornerMultNext * (aCornerDims - aInnerRadius);
  // Connect edge to inner arc end point
  Point innerCornerEnd =
      outerCornerEnd + aCornerMultPrev * (aCornerDims - aInnerRadius);

  // Outer arc start point
  Point outerArcStart = aOuterCorner + aCornerMultPrev * aOuterRadius;
  // Outer arc end point
  Point outerArcEnd = aOuterCorner + aCornerMultNext * aOuterRadius;
  // Inner arc start point
  Point innerArcStart = aInnerCorner + aCornerMultPrev * aInnerRadius;
  // Inner arc end point
  Point innerArcEnd = aInnerCorner + aCornerMultNext * aInnerRadius;

  // Outer radius center
  Point outerCenter =
      aOuterCorner + (aCornerMultPrev + aCornerMultNext) * aOuterRadius;
  // Inner radius center
  Point innerCenter =
      aInnerCorner + (aCornerMultPrev + aCornerMultNext) * aInnerRadius;

  RefPtr<PathBuilder> builder;
  RefPtr<Path> path;

  if (aFirstColor.a > 0) {
    builder = aDrawTarget->CreatePathBuilder();
    builder->MoveTo(outerCornerStart);
  }

  if (aFirstColor != aSecondColor) {
    // Start and end angles of corner quadrant
    constexpr float PIf = M_PI;
    Float startAngle = (static_cast<float>(c) * PIf) / 2.0f - PIf;
    Float endAngle = startAngle + PIf / 2.0f;
    Float outerSplitAngle, innerSplitAngle;
    Point outerSplit, innerSplit;

    // Outer half-way point
    SplitBorderRadius(outerCenter, aOuterRadius, aOuterCorner, aInnerCorner,
                      aCornerMultPrev + aCornerMultNext, startAngle, outerSplit,
                      outerSplitAngle);
    // Inner half-way point
    if (aInnerRadius.IsEmpty()) {
      innerSplit = aInnerCorner;
      innerSplitAngle = endAngle;
    } else {
      SplitBorderRadius(innerCenter, aInnerRadius, aOuterCorner, aInnerCorner,
                        aCornerMultPrev + aCornerMultNext, startAngle,
                        innerSplit, innerSplitAngle);
    }

    // Draw first half with first color
    if (aFirstColor.a > 0) {
      AcuteArcToBezier(builder.get(), outerCenter, aOuterRadius, outerArcStart,
                       outerSplit, startAngle, outerSplitAngle);
      // Draw skirt as part of first half
      if (aSkirtSize > 0) {
        builder->LineTo(outerSplit + aCornerMultNext * aSkirtSize);
        builder->LineTo(innerSplit -
                        aCornerMultPrev * (aSkirtSize * aSkirtSlope));
      }
      AcuteArcToBezier(builder.get(), innerCenter, aInnerRadius, innerSplit,
                       innerArcStart, innerSplitAngle, startAngle);
      if ((innerCornerStart - innerArcStart).DotProduct(aCornerMultPrev) > 0) {
        builder->LineTo(innerCornerStart);
      }
      builder->Close();
      path = builder->Finish();
      aDrawTarget->Fill(path, ColorPattern(aFirstColor));
    }

    // Draw second half with second color
    if (aSecondColor.a > 0) {
      builder = aDrawTarget->CreatePathBuilder();
      builder->MoveTo(outerCornerEnd);
      if ((innerArcEnd - innerCornerEnd).DotProduct(aCornerMultNext) < 0) {
        builder->LineTo(innerCornerEnd);
      }
      AcuteArcToBezier(builder.get(), innerCenter, aInnerRadius, innerArcEnd,
                       innerSplit, endAngle, innerSplitAngle);
      AcuteArcToBezier(builder.get(), outerCenter, aOuterRadius, outerSplit,
                       outerArcEnd, outerSplitAngle, endAngle);
      builder->Close();
      path = builder->Finish();
      aDrawTarget->Fill(path, ColorPattern(aSecondColor));
    }
  } else if (aFirstColor.a > 0) {
    // Draw corner with single color
    AcuteArcToBezier(builder.get(), outerCenter, aOuterRadius, outerArcStart,
                     outerArcEnd);
    builder->LineTo(outerCornerEnd);
    if ((innerArcEnd - innerCornerEnd).DotProduct(aCornerMultNext) < 0) {
      builder->LineTo(innerCornerEnd);
    }
    AcuteArcToBezier(builder.get(), innerCenter, aInnerRadius, innerArcEnd,
                     innerArcStart, -kKappaFactor);
    if ((innerCornerStart - innerArcStart).DotProduct(aCornerMultPrev) > 0) {
      builder->LineTo(innerCornerStart);
    }
    builder->Close();
    path = builder->Finish();
    aDrawTarget->Fill(path, ColorPattern(aFirstColor));
  }
}

// Draw a corner with possibly different sides.
// A skirt is drawn underneath the corner intersection to hide possible
// seams when anti-aliased drawing is used.
static void DrawCorner(DrawTarget* aDrawTarget, const Point& aOuterCorner,
                       const Point& aInnerCorner,
                       const twoFloats& aCornerMultPrev,
                       const twoFloats& aCornerMultNext,
                       const Size& aCornerDims, const DeviceColor& aFirstColor,
                       const DeviceColor& aSecondColor, Float aSkirtSize,
                       Float aSkirtSlope) {
  // Corner box start point
  Point cornerStart = aOuterCorner + aCornerMultPrev * aCornerDims;
  // Corner box end point
  Point cornerEnd = aOuterCorner + aCornerMultNext * aCornerDims;

  RefPtr<PathBuilder> builder;
  RefPtr<Path> path;

  if (aFirstColor.a > 0) {
    builder = aDrawTarget->CreatePathBuilder();
    builder->MoveTo(cornerStart);
  }

  if (aFirstColor != aSecondColor) {
    // Draw first half with first color
    if (aFirstColor.a > 0) {
      builder->LineTo(aOuterCorner);
      // Draw skirt as part of first half
      if (aSkirtSize > 0) {
        builder->LineTo(aOuterCorner + aCornerMultNext * aSkirtSize);
        builder->LineTo(aInnerCorner -
                        aCornerMultPrev * (aSkirtSize * aSkirtSlope));
      }
      builder->LineTo(aInnerCorner);
      builder->Close();
      path = builder->Finish();
      aDrawTarget->Fill(path, ColorPattern(aFirstColor));
    }

    // Draw second half with second color
    if (aSecondColor.a > 0) {
      builder = aDrawTarget->CreatePathBuilder();
      builder->MoveTo(cornerEnd);
      builder->LineTo(aInnerCorner);
      builder->LineTo(aOuterCorner);
      builder->Close();
      path = builder->Finish();
      aDrawTarget->Fill(path, ColorPattern(aSecondColor));
    }
  } else if (aFirstColor.a > 0) {
    // Draw corner with single color
    builder->LineTo(aOuterCorner);
    builder->LineTo(cornerEnd);
    builder->LineTo(aInnerCorner);
    builder->Close();
    path = builder->Finish();
    aDrawTarget->Fill(path, ColorPattern(aFirstColor));
  }
}

void nsCSSBorderRenderer::DrawSolidBorder() {
  const twoFloats cornerMults[4] = {{-1, 0}, {0, -1}, {+1, 0}, {0, +1}};

  const twoFloats centerAdjusts[4] = {
      {0, +0.5}, {-0.5, 0}, {0, -0.5}, {+0.5, 0}};

  RectCornerRadii innerRadii;
  ComputeInnerRadii(mBorderRadii, mBorderWidths, &innerRadii);

  Rect strokeRect = mOuterRect;
  strokeRect.Deflate(
      Margin(mBorderWidths.top / 2.0f, mBorderWidths.right / 2.0f,
             mBorderWidths.bottom / 2.0f, mBorderWidths.left / 2.0f));

  for (const auto i : mozilla::AllPhysicalSides()) {
    // We now draw the current side and the CW corner following it.
    // The CCW corner of this side was already drawn in the previous iteration.
    // The side will be drawn as an explicit stroke, and the CW corner will be
    // filled separately.
    // If the next side does not have a matching color, then we split the
    // corner into two halves, one of each side's color and draw both.
    // Thus, the CCW corner of the next side will end up drawn here.

    // the corner index -- either 1 2 3 0 (cw) or 0 3 2 1 (ccw)
    Corner c = Corner((i + 1) % 4);
    Corner prevCorner = Corner(i);

    // i+2 and i+3 respectively.  These are used to index into the corner
    // multiplier table, and were deduced by calculating out the long form
    // of each corner and finding a pattern in the signs and values.
    auto i1 = Side((i + 1) % 4);
    auto i2 = Side((i + 2) % 4);
    auto i3 = Side((i + 3) % 4);

    Float sideWidth = 0.0f;
    DeviceColor firstColor, secondColor;
    if (IsVisible(mBorderStyles[i]) && mBorderWidths.Side(i) != 0.0f) {
      // draw the side since it is visible
      sideWidth = mBorderWidths.Side(i);
      firstColor = ToDeviceColor(mBorderColors[i]);
      // if the next side is visible, use its color for corner
      secondColor =
          IsVisible(mBorderStyles[i1]) && mBorderWidths.Side(i1) != 0.0f
              ? ToDeviceColor(mBorderColors[i1])
              : firstColor;
    } else if (IsVisible(mBorderStyles[i1]) && mBorderWidths.Side(i1) != 0.0f) {
      // assign next side's color to both corner sides
      firstColor = ToDeviceColor(mBorderColors[i1]);
      secondColor = firstColor;
    } else {
      // neither side is visible, so nothing to do
      continue;
    }

    Point outerCorner = mOuterRect.AtCorner(c);
    Point innerCorner = mInnerRect.AtCorner(c);

    // start and end points of border side stroke between corners
    Point sideStart = mOuterRect.AtCorner(prevCorner) +
                      cornerMults[i2] * mBorderCornerDimensions[prevCorner];
    Point sideEnd = outerCorner + cornerMults[i] * mBorderCornerDimensions[c];
    // check if the side is visible and not inverted
    if (sideWidth > 0 && firstColor.a > 0 &&
        -(sideEnd - sideStart).DotProduct(cornerMults[i]) > 0) {
      mDrawTarget->StrokeLine(sideStart + centerAdjusts[i] * sideWidth,
                              sideEnd + centerAdjusts[i] * sideWidth,
                              ColorPattern(firstColor),
                              StrokeOptions(sideWidth));
    }

    Float skirtSize = 0.0f, skirtSlope = 0.0f;
    // the sides don't match, so compute a skirt
    if (firstColor != secondColor &&
        mPresContext->Type() != nsPresContext::eContext_Print) {
      Point cornerDir = outerCorner - innerCorner;
      ComputeCornerSkirtSize(
          firstColor.a, secondColor.a, cornerDir.DotProduct(cornerMults[i]),
          cornerDir.DotProduct(cornerMults[i3]), skirtSize, skirtSlope);
    }

    if (!mBorderRadii[c].IsEmpty()) {
      // the corner has a border radius
      DrawBorderRadius(mDrawTarget, c, outerCorner, innerCorner, cornerMults[i],
                       cornerMults[i3], mBorderCornerDimensions[c],
                       mBorderRadii[c], innerRadii[c], firstColor, secondColor,
                       skirtSize, skirtSlope);
    } else if (!mBorderCornerDimensions[c].IsEmpty()) {
      // a corner with no border radius
      DrawCorner(mDrawTarget, outerCorner, innerCorner, cornerMults[i],
                 cornerMults[i3], mBorderCornerDimensions[c], firstColor,
                 secondColor, skirtSize, skirtSlope);
    }
  }
}

void nsCSSBorderRenderer::DrawBorders() {
  if (MOZ_UNLIKELY(!mDirtyRect.Intersects(mOuterRect))) {
    return;
  }

  if (mAllBordersSameStyle && (mBorderStyles[0] == StyleBorderStyle::None ||
                               mBorderStyles[0] == StyleBorderStyle::Hidden ||
                               mBorderColors[0] == NS_RGBA(0, 0, 0, 0))) {
    // All borders are the same style, and the style is either none or hidden,
    // or the color is transparent.
    return;
  }

  if (mAllBordersSameWidth && mBorderWidths.top == 0.0) {
    // Some of the mAllBordersSameWidth codepaths depend on the border
    // width being greater than zero.
    return;
  }

  AutoRestoreTransform autoRestoreTransform;
  Matrix mat = mDrawTarget->GetTransform();

  // Clamp the CTM to be pixel-aligned; we do this only
  // for translation-only matrices now, but we could do it
  // if the matrix has just a scale as well.  We should not
  // do it if there's a rotation.
  if (mat.HasNonTranslation()) {
    if (!mat.HasNonAxisAlignedTransform()) {
      // Scale + transform. Avoid stroke fast-paths so that we have a chance
      // of snapping to pixel boundaries.
      mAvoidStroke = true;
    }
  } else {
    mat._31 = floor(mat._31 + 0.5);
    mat._32 = floor(mat._32 + 0.5);
    autoRestoreTransform.Init(mDrawTarget);
    mDrawTarget->SetTransform(mat);

    // round mOuterRect and mInnerRect; they're already an integer
    // number of pixels apart and should stay that way after
    // rounding. We don't do this if there's a scale in the current transform
    // since this loses information that might be relevant when we're scaling.
    mOuterRect.Round();
    mInnerRect.Round();
  }

  // Initial values only used when the border colors/widths are all the same:
  ColorPattern color(ToDeviceColor(mBorderColors[eSideTop]));
  StrokeOptions strokeOptions(mBorderWidths.top);

  // First there's a couple of 'special cases' that have specifically optimized
  // drawing paths, when none of these can be used we move on to the generalized
  // border drawing code.
  if (mAllBordersSameStyle && mAllBordersSameWidth &&
      mBorderStyles[0] == StyleBorderStyle::Solid && mNoBorderRadius &&
      !mAvoidStroke) {
    // Very simple case.
    Rect rect = mOuterRect;
    rect.Deflate(mBorderWidths.top / 2.0);
    mDrawTarget->StrokeRect(rect, color, strokeOptions);
    return;
  }

  if (mAllBordersSameStyle && mBorderStyles[0] == StyleBorderStyle::Solid &&
      !mAvoidStroke && !mNoBorderRadius) {
    // Relatively simple case.
    RoundedRect borderInnerRect(mOuterRect, mBorderRadii);
    borderInnerRect.Deflate(mBorderWidths);

    // Instead of stroking we just use two paths: an inner and an outer.
    // This allows us to draw borders that we couldn't when stroking. For
    // example, borders with a border width >= the border radius. (i.e. when
    // there are square corners on the inside)
    //
    // Further, this approach can be more efficient because the backend
    // doesn't need to compute an offset curve to stroke the path. We know that
    // the rounded parts are elipses we can offset exactly and can just compute
    // a new cubic approximation.
    RefPtr<PathBuilder> builder = mDrawTarget->CreatePathBuilder();
    AppendRoundedRectToPath(builder, mOuterRect, mBorderRadii, true);
    AppendRoundedRectToPath(builder, borderInnerRect.rect,
                            borderInnerRect.corners, false);
    RefPtr<Path> path = builder->Finish();
    mDrawTarget->Fill(path, color);
    return;
  }

  const bool allBordersSolid = AllBordersSolid();

  // This leaves the border corners non-interpolated for single width borders.
  // Doing this is slightly faster and shouldn't be a problem visually.
  if (allBordersSolid && mAllBordersSameWidth && mBorderWidths.top == 1 &&
      mNoBorderRadius && !mAvoidStroke) {
    DrawSingleWidthSolidBorder();
    return;
  }

  if (allBordersSolid && !mAvoidStroke) {
    DrawSolidBorder();
    return;
  }

  PrintAsString(" mOuterRect: ");
  PrintAsString(mOuterRect);
  PrintAsStringNewline();
  PrintAsString(" mInnerRect: ");
  PrintAsString(mInnerRect);
  PrintAsStringNewline();
  PrintAsFormatString(" mBorderColors: 0x%08x 0x%08x 0x%08x 0x%08x\n",
                      mBorderColors[0], mBorderColors[1], mBorderColors[2],
                      mBorderColors[3]);

  // if conditioning the outside rect failed, then bail -- the outside
  // rect is supposed to enclose the entire border
  {
    gfxRect outerRect = ThebesRect(mOuterRect);
    gfxUtils::ConditionRect(outerRect);
    if (outerRect.IsEmpty()) {
      return;
    }
    mOuterRect = ToRect(outerRect);

    if (MOZ_UNLIKELY(!mDirtyRect.Intersects(mOuterRect))) {
      return;
    }

    gfxRect innerRect = ThebesRect(mInnerRect);
    gfxUtils::ConditionRect(innerRect);
    mInnerRect = ToRect(innerRect);
  }

  SideBits dashedSides = SideBits::eNone;
  bool forceSeparateCorners = false;

  for (const auto i : mozilla::AllPhysicalSides()) {
    StyleBorderStyle style = mBorderStyles[i];
    if (style == StyleBorderStyle::Dashed ||
        style == StyleBorderStyle::Dotted) {
      // we need to draw things separately for dashed/dotting
      forceSeparateCorners = true;
      dashedSides |= static_cast<mozilla::SideBits>(1 << i);
    }
  }

  PrintAsFormatString(" mAllBordersSameStyle: %d dashedSides: 0x%02x\n",
                      mAllBordersSameStyle,
                      static_cast<unsigned int>(dashedSides));

  if (mAllBordersSameStyle && !forceSeparateCorners) {
    /* Draw everything in one go */
    DrawBorderSides(SideBits::eAll);
    PrintAsStringNewline("---------------- (1)");
  } else {
    AUTO_PROFILER_LABEL("nsCSSBorderRenderer::DrawBorders:multipass", GRAPHICS);

    /* We have more than one pass to go.  Draw the corners separately from the
     * sides. */

    // The corner is going to have negligible size if its two adjacent border
    // sides are only 1px wide and there is no border radius.  In that case we
    // skip the overhead of painting the corner by setting the width or height
    // of the corner to zero, which effectively extends one of the corner's
    // adjacent border sides.  We extend the longer adjacent side so that
    // opposite sides will be the same length, which is necessary for opposite
    // dashed/dotted sides to be symmetrical.
    //
    //   if width > height
    //     +--+--------------+--+    +--------------------+
    //     |  |              |  |    |                    |
    //     +--+--------------+--+    +--+--------------+--+
    //     |  |              |  |    |  |              |  |
    //     |  |              |  | => |  |              |  |
    //     |  |              |  |    |  |              |  |
    //     +--+--------------+--+    +--+--------------+--+
    //     |  |              |  |    |                    |
    //     +--+--------------+--+    +--------------------+
    //
    //   if width <= height
    //     +--+--------+--+    +--+--------+--+
    //     |  |        |  |    |  |        |  |
    //     +--+--------+--+    |  +--------+  |
    //     |  |        |  |    |  |        |  |
    //     |  |        |  |    |  |        |  |
    //     |  |        |  |    |  |        |  |
    //     |  |        |  | => |  |        |  |
    //     |  |        |  |    |  |        |  |
    //     |  |        |  |    |  |        |  |
    //     |  |        |  |    |  |        |  |
    //     +--+--------+--+    |  +--------+  |
    //     |  |        |  |    |  |        |  |
    //     +--+--------+--+    +--+--------+--+
    //
    // Note that if we have different border widths we could end up with
    // opposite sides of different length.  For example, if the left and
    // bottom borders are 2px wide instead of 1px, we will end up doing
    // something like:
    //
    //     +----+------------+--+    +----+---------------+
    //     |    |            |  |    |    |               |
    //     +----+------------+--+    +----+------------+--+
    //     |    |            |  |    |    |            |  |
    //     |    |            |  | => |    |            |  |
    //     |    |            |  |    |    |            |  |
    //     +----+------------+--+    +----+------------+--+
    //     |    |            |  |    |    |            |  |
    //     |    |            |  |    |    |            |  |
    //     +----+------------+--+    +----+------------+--+
    //
    // XXX Should we only do this optimization if |mAllBordersSameWidth| is
    // true?
    //
    // XXX In fact is this optimization even worth the complexity it adds to
    // the code?  1px wide dashed borders are not overly common, and drawing
    // corners for them is not that expensive.
    for (const auto corner : mozilla::AllPhysicalCorners()) {
      const mozilla::Side sides[2] = {mozilla::Side(corner), PREV_SIDE(corner)};

      if (!IsZeroSize(mBorderRadii[corner])) {
        continue;
      }

      if (mBorderWidths.Side(sides[0]) == 1.0 &&
          mBorderWidths.Side(sides[1]) == 1.0) {
        if (mOuterRect.Width() > mOuterRect.Height()) {
          mBorderCornerDimensions[corner].width = 0.0;
        } else {
          mBorderCornerDimensions[corner].height = 0.0;
        }
      }
    }

    // First, the corners
    for (const auto corner : mozilla::AllPhysicalCorners()) {
      // if there's no corner, don't do all this work for it
      if (IsZeroSize(mBorderCornerDimensions[corner])) {
        continue;
      }

      const int sides[2] = {corner, PREV_SIDE(corner)};
      SideBits sideBits =
          static_cast<SideBits>((1 << sides[0]) | (1 << sides[1]));

      bool simpleCornerStyle = AreBorderSideFinalStylesSame(sideBits);

      // If we don't have anything complex going on in this corner,
      // then we can just fill the corner with a solid color, and avoid
      // the potentially expensive clip.
      if (simpleCornerStyle && IsZeroSize(mBorderRadii[corner]) &&
          IsSolidCornerStyle(mBorderStyles[sides[0]], corner)) {
        sRGBColor color = MakeBorderColor(
            mBorderColors[sides[0]],
            BorderColorStyleForSolidCorner(mBorderStyles[sides[0]], corner));
        mDrawTarget->FillRect(GetCornerRect(corner),
                              ColorPattern(ToDeviceColor(color)));
        continue;
      }

      // clip to the corner
      mDrawTarget->PushClipRect(GetCornerRect(corner));

      if (simpleCornerStyle) {
        // we don't need a group for this corner, the sides are the same,
        // but we weren't able to render just a solid block for the corner.
        DrawBorderSides(sideBits);
      } else {
        // Sides are different.  We could draw using OP_ADD to
        // get correct color blending behaviour at the seam.  We'd need
        // to do it in an offscreen surface to ensure that we're
        // always compositing on transparent black.  If the colors
        // don't have transparency and the current destination surface
        // has an alpha channel, we could just clear the region and
        // avoid the temporary, but that situation doesn't happen all
        // that often in practice (we double buffer to no-alpha
        // surfaces). We choose just to seam though, as the performance
        // advantages outway the modest easthetic improvement.

        for (int cornerSide = 0; cornerSide < 2; cornerSide++) {
          mozilla::Side side = mozilla::Side(sides[cornerSide]);
          StyleBorderStyle style = mBorderStyles[side];

          PrintAsFormatString("corner: %d cornerSide: %d side: %d style: %d\n",
                              corner, cornerSide, side,
                              static_cast<int>(style));

          RefPtr<Path> path = GetSideClipSubPath(side);
          mDrawTarget->PushClip(path);

          DrawBorderSides(static_cast<mozilla::SideBits>(1 << side));

          mDrawTarget->PopClip();
        }
      }

      mDrawTarget->PopClip();

      PrintAsStringNewline();
    }

    // in the case of a single-unit border, we already munged the
    // corners up above; so we can just draw the top left and bottom
    // right sides separately, if they're the same.
    //
    // We need to check for mNoBorderRadius, because when there is
    // one, FillSolidBorder always draws the full rounded rectangle
    // and expects there to be a clip in place.
    SideBits alreadyDrawnSides = SideBits::eNone;
    if (mOneUnitBorder && mNoBorderRadius &&
        (dashedSides & (SideBits::eTop | SideBits::eLeft)) == SideBits::eNone) {
      bool tlBordersSameStyle =
          AreBorderSideFinalStylesSame(SideBits::eTop | SideBits::eLeft);
      bool brBordersSameStyle =
          AreBorderSideFinalStylesSame(SideBits::eBottom | SideBits::eRight);

      if (tlBordersSameStyle) {
        DrawBorderSides(SideBits::eTop | SideBits::eLeft);
        alreadyDrawnSides |= (SideBits::eTop | SideBits::eLeft);
      }

      if (brBordersSameStyle &&
          (dashedSides & (SideBits::eBottom | SideBits::eRight)) ==
              SideBits::eNone) {
        DrawBorderSides(SideBits::eBottom | SideBits::eRight);
        alreadyDrawnSides |= (SideBits::eBottom | SideBits::eRight);
      }
    }

    // We're done with the corners, now draw the sides.
    for (const auto side : mozilla::AllPhysicalSides()) {
      // if we drew it above, skip it
      if (alreadyDrawnSides & static_cast<mozilla::SideBits>(1 << side)) {
        continue;
      }

      // If there's no border on this side, skip it
      if (mBorderWidths.Side(side) == 0.0 ||
          mBorderStyles[side] == StyleBorderStyle::Hidden ||
          mBorderStyles[side] == StyleBorderStyle::None) {
        continue;
      }

      if (dashedSides & static_cast<mozilla::SideBits>(1 << side)) {
        // Dashed sides will always draw just the part ignoring the
        // corners for the side, so no need to clip.
        DrawDashedOrDottedSide(side);

        PrintAsStringNewline("---------------- (d)");
        continue;
      }

      // Undashed sides will currently draw the entire side,
      // including parts that would normally be covered by a corner,
      // so we need to clip.
      //
      // XXX Optimization -- it would be good to make this work like
      // DrawDashedOrDottedSide, and have a DrawOneSide function that just
      // draws one side and not the corners, because then we can
      // avoid the potentially expensive clip.
      mDrawTarget->PushClipRect(GetSideClipWithoutCornersRect(side));

      DrawBorderSides(static_cast<mozilla::SideBits>(1 << side));

      mDrawTarget->PopClip();

      PrintAsStringNewline("---------------- (*)");
    }
  }
}

void nsCSSBorderRenderer::CreateWebRenderCommands(
    nsDisplayItem* aItem, wr::DisplayListBuilder& aBuilder,
    wr::IpcResourceUpdateQueue& aResources,
    const layers::StackingContextHelper& aSc) {
  LayoutDeviceRect outerRect = LayoutDeviceRect::FromUnknownRect(mOuterRect);
  wr::LayoutRect roundedRect = wr::ToLayoutRect(outerRect);
  wr::LayoutRect clipRect = roundedRect;
  wr::BorderSide side[4];
  for (const auto i : mozilla::AllPhysicalSides()) {
    side[i] =
        wr::ToBorderSide(ToDeviceColor(mBorderColors[i]), mBorderStyles[i]);
  }

  wr::LayoutSideOffsets borderWidths = wr::ToBorderWidths(mBorderWidths);
  wr::BorderRadius borderRadius = wr::ToBorderRadius(mBorderRadii);
  wr::LayoutSideOffsets borderInset = wr::ToLayoutSideOffsets(mBorderInset);

  if (mLocalClip) {
    LayoutDeviceRect localClip =
        LayoutDeviceRect::FromUnknownRect(mLocalClip.value());
    clipRect = wr::ToLayoutRect(localClip.Intersect(outerRect));
  }

  Range<const wr::BorderSide> wrsides(side, 4);
  aBuilder.PushBorder(roundedRect, clipRect, mBackfaceIsVisible, borderWidths,
                      wrsides, borderRadius, borderInset);
}

/* static */
Maybe<nsCSSBorderImageRenderer>
nsCSSBorderImageRenderer::CreateBorderImageRenderer(
    nsPresContext* aPresContext, nsIFrame* aForFrame, const nsRect& aBorderArea,
    const nsStyleBorder& aStyleBorder, const nsRect& aDirtyRect,
    Sides aSkipSides, uint32_t aFlags, ImgDrawResult* aDrawResult) {
  MOZ_ASSERT(aDrawResult);

  if (aDirtyRect.IsEmpty()) {
    *aDrawResult = ImgDrawResult::SUCCESS;
    return Nothing();
  }

  nsImageRenderer imgRenderer(aForFrame, &aStyleBorder.mBorderImageSource,
                              aFlags);
  if (!imgRenderer.PrepareImage()) {
    *aDrawResult = imgRenderer.PrepareResult();
    return Nothing();
  }

  // We should always get here with the frame's border, but we may construct an
  // nsStyleBorder om the stack to deal with :visited and other shenaningans.
  //
  // We always copy the border image and such from the non-visited one, so
  // there's no need to do anything with it.
  MOZ_ASSERT(aStyleBorder.GetBorderImageRequest() ==
             aForFrame->StyleBorder()->GetBorderImageRequest());

  nsCSSBorderImageRenderer renderer(aForFrame, aBorderArea, aStyleBorder,
                                    aSkipSides, imgRenderer);
  *aDrawResult = ImgDrawResult::SUCCESS;
  return Some(renderer);
}

ImgDrawResult nsCSSBorderImageRenderer::DrawBorderImage(
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    nsIFrame* aForFrame, const nsRect& aDirtyRect) {
  // NOTE: no Save() yet, we do that later by calling autoSR.EnsureSaved()
  // in case we need it.
  gfxContextAutoSaveRestore autoSR;

  if (!mClip.IsEmpty()) {
    autoSR.EnsureSaved(&aRenderingContext);
    aRenderingContext.Clip(NSRectToSnappedRect(
        mClip, aForFrame->PresContext()->AppUnitsPerDevPixel(),
        *aRenderingContext.GetDrawTarget()));
  }

  // intrinsicSize.CanComputeConcreteSize() return false means we can not
  // read intrinsic size from aStyleBorder.mBorderImageSource.
  // In this condition, we pass imageSize(a resolved size comes from
  // default sizing algorithm) to renderer as the viewport size.
  CSSSizeOrRatio intrinsicSize = mImageRenderer.ComputeIntrinsicSize();
  Maybe<CSSSize> svgViewportSize =
      intrinsicSize.CanComputeConcreteSize()
          ? Nothing()
          : Some(CSSSize::FromAppUnits(mImageSize));
  bool hasIntrinsicRatio = intrinsicSize.HasRatio();

  // These helper tables recharacterize the 'slice' and 'width' margins
  // in a more convenient form: they are the x/y/width/height coords
  // required for various bands of the border, and they have been transformed
  // to be relative to the innerRect (for 'slice') or the page (for 'border').
  enum { LEFT, MIDDLE, RIGHT, TOP = LEFT, BOTTOM = RIGHT };
  const nscoord borderX[3] = {
      mArea.x + 0,
      mArea.x + mWidths.left,
      mArea.x + mArea.width - mWidths.right,
  };
  const nscoord borderY[3] = {
      mArea.y + 0,
      mArea.y + mWidths.top,
      mArea.y + mArea.height - mWidths.bottom,
  };
  const nscoord borderWidth[3] = {
      mWidths.left,
      mArea.width - mWidths.left - mWidths.right,
      mWidths.right,
  };
  const nscoord borderHeight[3] = {
      mWidths.top,
      mArea.height - mWidths.top - mWidths.bottom,
      mWidths.bottom,
  };
  const int32_t sliceX[3] = {
      0,
      mSlice.left,
      mImageSize.width - mSlice.right,
  };
  const int32_t sliceY[3] = {
      0,
      mSlice.top,
      mImageSize.height - mSlice.bottom,
  };
  const int32_t sliceWidth[3] = {
      mSlice.left,
      std::max(mImageSize.width - mSlice.left - mSlice.right, 0),
      mSlice.right,
  };
  const int32_t sliceHeight[3] = {
      mSlice.top,
      std::max(mImageSize.height - mSlice.top - mSlice.bottom, 0),
      mSlice.bottom,
  };

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  for (int i = LEFT; i <= RIGHT; i++) {
    for (int j = TOP; j <= BOTTOM; j++) {
      StyleBorderImageRepeatKeyword fillStyleH, fillStyleV;
      nsSize unitSize;

      if (i == MIDDLE && j == MIDDLE) {
        // Discard the middle portion unless set to fill.
        if (!mFill) {
          continue;
        }

        // css-background:
        //     The middle image's width is scaled by the same factor as the
        //     top image unless that factor is zero or infinity, in which
        //     case the scaling factor of the bottom is substituted, and
        //     failing that, the width is not scaled. The height of the
        //     middle image is scaled by the same factor as the left image
        //     unless that factor is zero or infinity, in which case the
        //     scaling factor of the right image is substituted, and failing
        //     that, the height is not scaled.
        gfxFloat hFactor, vFactor;

        if (0 < mWidths.left && 0 < mSlice.left) {
          vFactor = gfxFloat(mWidths.left) / mSlice.left;
        } else if (0 < mWidths.right && 0 < mSlice.right) {
          vFactor = gfxFloat(mWidths.right) / mSlice.right;
        } else {
          vFactor = 1;
        }

        if (0 < mWidths.top && 0 < mSlice.top) {
          hFactor = gfxFloat(mWidths.top) / mSlice.top;
        } else if (0 < mWidths.bottom && 0 < mSlice.bottom) {
          hFactor = gfxFloat(mWidths.bottom) / mSlice.bottom;
        } else {
          hFactor = 1;
        }

        unitSize.width = sliceWidth[i] * hFactor;
        unitSize.height = sliceHeight[j] * vFactor;
        fillStyleH = mRepeatModeHorizontal;
        fillStyleV = mRepeatModeVertical;

      } else if (i == MIDDLE) {  // top, bottom
        // Sides are always stretched to the thickness of their border,
        // and stretched proportionately on the other axis.
        gfxFloat factor;
        if (0 < borderHeight[j] && 0 < sliceHeight[j]) {
          factor = gfxFloat(borderHeight[j]) / sliceHeight[j];
        } else {
          factor = 1;
        }

        unitSize.width = sliceWidth[i] * factor;
        unitSize.height = borderHeight[j];
        fillStyleH = mRepeatModeHorizontal;
        fillStyleV = StyleBorderImageRepeatKeyword::Stretch;

      } else if (j == MIDDLE) {  // left, right
        gfxFloat factor;
        if (0 < borderWidth[i] && 0 < sliceWidth[i]) {
          factor = gfxFloat(borderWidth[i]) / sliceWidth[i];
        } else {
          factor = 1;
        }

        unitSize.width = borderWidth[i];
        unitSize.height = sliceHeight[j] * factor;
        fillStyleH = StyleBorderImageRepeatKeyword::Stretch;
        fillStyleV = mRepeatModeVertical;

      } else {
        // Corners are always stretched to fit the corner.
        unitSize.width = borderWidth[i];
        unitSize.height = borderHeight[j];
        fillStyleH = StyleBorderImageRepeatKeyword::Stretch;
        fillStyleV = StyleBorderImageRepeatKeyword::Stretch;
      }

      nsRect destArea(borderX[i], borderY[j], borderWidth[i], borderHeight[j]);
      nsRect subArea(sliceX[i], sliceY[j], sliceWidth[i], sliceHeight[j]);
      if (subArea.IsEmpty()) {
        continue;
      }

      nsIntRect intSubArea = subArea.ToOutsidePixels(AppUnitsPerCSSPixel());
      result &= mImageRenderer.DrawBorderImageComponent(
          aPresContext, aRenderingContext, aDirtyRect, destArea,
          CSSIntRect(intSubArea.x, intSubArea.y, intSubArea.width,
                     intSubArea.height),
          fillStyleH, fillStyleV, unitSize, j * (RIGHT + 1) + i,
          svgViewportSize, hasIntrinsicRatio);
    }
  }

  return result;
}

ImgDrawResult nsCSSBorderImageRenderer::CreateWebRenderCommands(
    nsDisplayItem* aItem, nsIFrame* aForFrame,
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  if (!mImageRenderer.IsReady()) {
    return ImgDrawResult::NOT_READY;
  }

  float widths[4];
  float slice[4];
  const int32_t appUnitsPerDevPixel =
      aForFrame->PresContext()->AppUnitsPerDevPixel();
  for (const auto i : mozilla::AllPhysicalSides()) {
    slice[i] = (float)(mSlice.Side(i)) / appUnitsPerDevPixel;
    widths[i] = (float)(mWidths.Side(i)) / appUnitsPerDevPixel;
  }

  LayoutDeviceRect destRect =
      LayoutDeviceRect::FromAppUnits(mArea, appUnitsPerDevPixel);
  destRect.Round();
  wr::LayoutRect dest = wr::ToLayoutRect(destRect);

  wr::LayoutRect clip = dest;
  if (!mClip.IsEmpty()) {
    LayoutDeviceRect clipRect =
        LayoutDeviceRect::FromAppUnits(mClip, appUnitsPerDevPixel);
    clip = wr::ToLayoutRect(clipRect);
  }

  ImgDrawResult drawResult = ImgDrawResult::SUCCESS;
  switch (mImageRenderer.GetType()) {
    case StyleImage::Tag::Url: {
      RefPtr<imgIContainer> img = mImageRenderer.GetImage();
      if (!img || img->GetType() == imgIContainer::TYPE_VECTOR) {
        // Vector images will redraw each segment of the border up to 8 times.
        // We draw using a restricted region derived from the segment's clip and
        // scale the image accordingly (see ClippedImage::Draw). If we follow
        // this convention as is for WebRender, we will need to rasterize the
        // entire vector image scaled up without the restriction region, which
        // means our main thread CPU and memory footprints will be much higher.
        // Ideally we would be able to provide a raster image for each segment
        // of the border. For now we use fallback.
        return ImgDrawResult::NOT_SUPPORTED;
      }

      uint32_t flags = aDisplayListBuilder->GetImageDecodeFlags();

      LayoutDeviceRect imageRect = LayoutDeviceRect::FromAppUnits(
          nsRect(nsPoint(), mImageRenderer.GetSize()), appUnitsPerDevPixel);

      SVGImageContext svgContext;
      Maybe<ImageIntRegion> region;
      gfx::IntSize decodeSize =
          nsLayoutUtils::ComputeImageContainerDrawingParameters(
              img, aForFrame, imageRect, imageRect, aSc, flags, svgContext,
              region);

      RefPtr<WebRenderImageProvider> provider;
      drawResult = img->GetImageProvider(aManager->LayerManager(), decodeSize,
                                         svgContext, region, flags,
                                         getter_AddRefs(provider));

      Maybe<wr::ImageKey> key =
          aManager->CommandBuilder().CreateImageProviderKey(
              aItem, provider, drawResult, aResources);
      if (key.isNothing()) {
        break;
      }

      auto rendering =
          wr::ToImageRendering(aItem->Frame()->UsedImageRendering());
      if (mFill) {
        float epsilon = 0.0001;
        bool noVerticalBorders = widths[0] <= epsilon && widths[2] < epsilon;
        bool noHorizontalBorders = widths[1] <= epsilon && widths[3] < epsilon;

        // Border image with no border. It's a little silly but WebRender
        // currently does not handle this. We could fall back to a blob image
        // but there are reftests that are sensible to the test going through a
        // blob while the reference doesn't.
        if (noVerticalBorders && noHorizontalBorders) {
          aBuilder.PushImage(dest, clip, !aItem->BackfaceIsHidden(), false,
                             rendering, key.value());
          break;
        }

        // Fall-back if we want to fill the middle area and opposite edges are
        // both empty.
        // TODO(bug 1609893): moving some of the repetition handling code out
        // of the image shader will make it easier to handle these cases
        // properly.
        if (noHorizontalBorders || noVerticalBorders) {
          return ImgDrawResult::NOT_SUPPORTED;
        }
      }

      wr::WrBorderImage params{
          wr::ToBorderWidths(widths[0], widths[1], widths[2], widths[3]),
          key.value(),
          rendering,
          mImageSize.width / appUnitsPerDevPixel,
          mImageSize.height / appUnitsPerDevPixel,
          mFill,
          wr::ToDeviceIntSideOffsets(slice[0], slice[1], slice[2], slice[3]),
          wr::ToRepeatMode(mRepeatModeHorizontal),
          wr::ToRepeatMode(mRepeatModeVertical)};

      aBuilder.PushBorderImage(dest, clip, !aItem->BackfaceIsHidden(), params);
      break;
    }
    case StyleImage::Tag::Gradient: {
      const StyleGradient& gradient = *mImageRenderer.GetGradientData();
      nsCSSGradientRenderer renderer = nsCSSGradientRenderer::Create(
          aForFrame->PresContext(), aForFrame->Style(), gradient, mImageSize);

      wr::ExtendMode extendMode;
      nsTArray<wr::GradientStop> stops;
      LayoutDevicePoint lineStart;
      LayoutDevicePoint lineEnd;
      LayoutDeviceSize gradientRadius;
      LayoutDevicePoint gradientCenter;
      float gradientAngle;
      renderer.BuildWebRenderParameters(1.0, extendMode, stops, lineStart,
                                        lineEnd, gradientRadius, gradientCenter,
                                        gradientAngle);

      if (gradient.IsLinear()) {
        LayoutDevicePoint startPoint =
            LayoutDevicePoint(dest.min.x, dest.min.y) + lineStart;
        LayoutDevicePoint endPoint =
            LayoutDevicePoint(dest.min.x, dest.min.y) + lineEnd;

        aBuilder.PushBorderGradient(
            dest, clip, !aItem->BackfaceIsHidden(),
            wr::ToBorderWidths(widths[0], widths[1], widths[2], widths[3]),
            (float)(mImageSize.width) / appUnitsPerDevPixel,
            (float)(mImageSize.height) / appUnitsPerDevPixel, mFill,
            wr::ToDeviceIntSideOffsets(slice[0], slice[1], slice[2], slice[3]),
            wr::ToLayoutPoint(startPoint), wr::ToLayoutPoint(endPoint), stops,
            extendMode);
      } else if (gradient.IsRadial()) {
        aBuilder.PushBorderRadialGradient(
            dest, clip, !aItem->BackfaceIsHidden(),
            wr::ToBorderWidths(widths[0], widths[1], widths[2], widths[3]),
            mFill, wr::ToLayoutPoint(lineStart),
            wr::ToLayoutSize(gradientRadius), stops, extendMode);
      } else {
        MOZ_ASSERT(gradient.IsConic());
        aBuilder.PushBorderConicGradient(
            dest, clip, !aItem->BackfaceIsHidden(),
            wr::ToBorderWidths(widths[0], widths[1], widths[2], widths[3]),
            mFill, wr::ToLayoutPoint(gradientCenter), gradientAngle, stops,
            extendMode);
      }
      break;
    }
    default:
      MOZ_ASSERT_UNREACHABLE("Unsupport border image type");
      drawResult = ImgDrawResult::NOT_SUPPORTED;
  }

  return drawResult;
}

nsCSSBorderImageRenderer::nsCSSBorderImageRenderer(
    const nsCSSBorderImageRenderer& aRhs)
    : mImageRenderer(aRhs.mImageRenderer),
      mImageSize(aRhs.mImageSize),
      mSlice(aRhs.mSlice),
      mWidths(aRhs.mWidths),
      mImageOutset(aRhs.mImageOutset),
      mArea(aRhs.mArea),
      mClip(aRhs.mClip),
      mRepeatModeHorizontal(aRhs.mRepeatModeHorizontal),
      mRepeatModeVertical(aRhs.mRepeatModeVertical),
      mFill(aRhs.mFill) {
  (void)mImageRenderer.PrepareResult();
}

nsCSSBorderImageRenderer& nsCSSBorderImageRenderer::operator=(
    const nsCSSBorderImageRenderer& aRhs) {
  mImageRenderer = aRhs.mImageRenderer;
  mImageSize = aRhs.mImageSize;
  mSlice = aRhs.mSlice;
  mWidths = aRhs.mWidths;
  mImageOutset = aRhs.mImageOutset;
  mArea = aRhs.mArea;
  mClip = aRhs.mClip;
  mRepeatModeHorizontal = aRhs.mRepeatModeHorizontal;
  mRepeatModeVertical = aRhs.mRepeatModeVertical;
  mFill = aRhs.mFill;
  (void)mImageRenderer.PrepareResult();

  return *this;
}

nsCSSBorderImageRenderer::nsCSSBorderImageRenderer(
    nsIFrame* aForFrame, const nsRect& aBorderArea,
    const nsStyleBorder& aStyleBorder, Sides aSkipSides,
    const nsImageRenderer& aImageRenderer)
    : mImageRenderer(aImageRenderer) {
  // Determine the border image area, which by default corresponds to the
  // border box but can be modified by 'border-image-outset'.
  // Note that 'border-radius' do not apply to 'border-image' borders per
  // <http://dev.w3.org/csswg/css-backgrounds/#corner-clipping>.
  nsMargin borderWidths(aStyleBorder.GetComputedBorder());
  mImageOutset = aStyleBorder.GetImageOutset();
  if (nsCSSRendering::IsBoxDecorationSlice(aStyleBorder) &&
      !aSkipSides.IsEmpty()) {
    mArea = nsCSSRendering::BoxDecorationRectForBorder(
        aForFrame, aBorderArea, aSkipSides, &aStyleBorder);
    if (mArea.IsEqualEdges(aBorderArea)) {
      // No need for a clip, just skip the sides we don't want.
      borderWidths.ApplySkipSides(aSkipSides);
      mImageOutset.ApplySkipSides(aSkipSides);
      mArea.Inflate(mImageOutset);
    } else {
      // We're drawing borders around the joined continuation boxes so we need
      // to clip that to the slice that we want for this frame.
      mArea.Inflate(mImageOutset);
      mImageOutset.ApplySkipSides(aSkipSides);
      mClip = aBorderArea;
      mClip.Inflate(mImageOutset);
    }
  } else {
    mArea = aBorderArea;
    mArea.Inflate(mImageOutset);
  }

  // Calculate the image size used to compute slice points.
  CSSSizeOrRatio intrinsicSize = mImageRenderer.ComputeIntrinsicSize();
  mImageSize = nsImageRenderer::ComputeConcreteSize(
      CSSSizeOrRatio(), intrinsicSize, mArea.Size());
  mImageRenderer.SetPreferredSize(intrinsicSize, mImageSize);

  // Compute the used values of 'border-image-slice' and 'border-image-width';
  // we do them together because the latter can depend on the former.
  for (const auto s : mozilla::AllPhysicalSides()) {
    const auto& slice = aStyleBorder.mBorderImageSlice.offsets.Get(s);
    int32_t imgDimension =
        SideIsVertical(s) ? mImageSize.width : mImageSize.height;
    nscoord borderDimension = SideIsVertical(s) ? mArea.width : mArea.height;
    double value;
    if (slice.IsNumber()) {
      value = nsPresContext::CSSPixelsToAppUnits(NS_lround(slice.AsNumber()));
    } else {
      MOZ_ASSERT(slice.IsPercentage());
      value = slice.AsPercentage()._0 * imgDimension;
    }
    if (value < 0) {
      value = 0;
    }
    if (value > imgDimension && imgDimension > 0) {
      value = imgDimension;
    }
    mSlice.Side(s) = value;

    const auto& width = aStyleBorder.mBorderImageWidth.Get(s);
    switch (width.tag) {
      case StyleBorderImageSideWidth::Tag::LengthPercentage:
        value =
            std::max(0, width.AsLengthPercentage().Resolve(borderDimension));
        break;
      case StyleBorderImageSideWidth::Tag::Number:
        value = width.AsNumber() * borderWidths.Side(s);
        break;
      case StyleBorderImageSideWidth::Tag::Auto:
        value = mSlice.Side(s);
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("unexpected CSS unit for border image area");
        value = 0;
        break;
    }
    // NSToCoordRoundWithClamp rounds towards infinity, but that's OK
    // because we expect value to be non-negative.
    MOZ_ASSERT(value >= 0);
    mWidths.Side(s) = NSToCoordRoundWithClamp(value);
    MOZ_ASSERT(mWidths.Side(s) >= 0);
  }

  // "If two opposite border-image-width offsets are large enough that they
  // overlap, their used values are proportionately reduced until they no
  // longer overlap."
  uint32_t combinedBorderWidth =
      uint32_t(mWidths.left) + uint32_t(mWidths.right);
  double scaleX = combinedBorderWidth > uint32_t(mArea.width)
                      ? mArea.width / double(combinedBorderWidth)
                      : 1.0;
  uint32_t combinedBorderHeight =
      uint32_t(mWidths.top) + uint32_t(mWidths.bottom);
  double scaleY = combinedBorderHeight > uint32_t(mArea.height)
                      ? mArea.height / double(combinedBorderHeight)
                      : 1.0;
  double scale = std::min(scaleX, scaleY);
  if (scale < 1.0) {
    mWidths.left *= scale;
    mWidths.right *= scale;
    mWidths.top *= scale;
    mWidths.bottom *= scale;
    NS_ASSERTION(mWidths.left + mWidths.right <= mArea.width &&
                     mWidths.top + mWidths.bottom <= mArea.height,
                 "rounding error in width reduction???");
  }

  mRepeatModeHorizontal = aStyleBorder.mBorderImageRepeat._0;
  mRepeatModeVertical = aStyleBorder.mBorderImageRepeat._1;
  mFill = aStyleBorder.mBorderImageSlice.fill;
}
