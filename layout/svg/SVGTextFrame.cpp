/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Main header first:
#include "SVGTextFrame.h"

// Keep others in (case-insensitive) order:
#include "DOMSVGPoint.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxFont.h"
#include "gfxSkipChars.h"
#include "gfxTypes.h"
#include "gfxUtils.h"
#include "LookAndFeel.h"
#include "nsAlgorithm.h"
#include "nsBidiPresUtils.h"
#include "nsBlockFrame.h"
#include "nsCaret.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsQuickSort.h"
#include "SVGPaintServerFrame.h"
#include "nsTArray.h"
#include "nsTextFrame.h"
#include "SVGAnimatedNumberList.h"
#include "SVGContentUtils.h"
#include "SVGContextPaint.h"
#include "SVGLengthList.h"
#include "SVGNumberList.h"
#include "nsLayoutUtils.h"
#include "nsFrameSelection.h"
#include "nsStyleStructInlines.h"
#include "mozilla/DisplaySVGItem.h"
#include "mozilla/Likely.h"
#include "mozilla/PresShell.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/SVGOuterSVGFrame.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/dom/DOMPointBinding.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/SVGGeometryElement.h"
#include "mozilla/dom/SVGRect.h"
#include "mozilla/dom/SVGTextContentElementBinding.h"
#include "mozilla/dom/SVGTextPathElement.h"
#include "mozilla/dom/Text.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/PatternHelpers.h"
#include <algorithm>
#include <cmath>
#include <limits>

using namespace mozilla::dom;
using namespace mozilla::dom::SVGTextContentElement_Binding;
using namespace mozilla::gfx;
using namespace mozilla::image;

namespace mozilla {

// ============================================================================
// Utility functions

/**
 * Using the specified gfxSkipCharsIterator, converts an offset and length
 * in original char indexes to skipped char indexes.
 *
 * @param aIterator The gfxSkipCharsIterator to use for the conversion.
 * @param aOriginalOffset The original offset.
 * @param aOriginalLength The original length.
 */
static gfxTextRun::Range ConvertOriginalToSkipped(
    gfxSkipCharsIterator& aIterator, uint32_t aOriginalOffset,
    uint32_t aOriginalLength) {
  uint32_t start = aIterator.ConvertOriginalToSkipped(aOriginalOffset);
  aIterator.AdvanceOriginal(aOriginalLength);
  return gfxTextRun::Range(start, aIterator.GetSkippedOffset());
}

/**
 * Converts an nsPoint from app units to user space units using the specified
 * nsPresContext and returns it as a gfxPoint.
 */
static gfxPoint AppUnitsToGfxUnits(const nsPoint& aPoint,
                                   const nsPresContext* aContext) {
  return gfxPoint(aContext->AppUnitsToGfxUnits(aPoint.x),
                  aContext->AppUnitsToGfxUnits(aPoint.y));
}

/**
 * Converts a gfxRect that is in app units to CSS pixels using the specified
 * nsPresContext and returns it as a gfxRect.
 */
static gfxRect AppUnitsToFloatCSSPixels(const gfxRect& aRect,
                                        const nsPresContext* aContext) {
  return gfxRect(nsPresContext::AppUnitsToFloatCSSPixels(aRect.x),
                 nsPresContext::AppUnitsToFloatCSSPixels(aRect.y),
                 nsPresContext::AppUnitsToFloatCSSPixels(aRect.width),
                 nsPresContext::AppUnitsToFloatCSSPixels(aRect.height));
}

/**
 * Returns whether a gfxPoint lies within a gfxRect.
 */
static bool Inside(const gfxRect& aRect, const gfxPoint& aPoint) {
  return aPoint.x >= aRect.x && aPoint.x < aRect.XMost() &&
         aPoint.y >= aRect.y && aPoint.y < aRect.YMost();
}

/**
 * Gets the measured ascent and descent of the text in the given nsTextFrame
 * in app units.
 *
 * @param aFrame The text frame.
 * @param aAscent The ascent in app units (output).
 * @param aDescent The descent in app units (output).
 */
static void GetAscentAndDescentInAppUnits(nsTextFrame* aFrame,
                                          gfxFloat& aAscent,
                                          gfxFloat& aDescent) {
  gfxSkipCharsIterator it = aFrame->EnsureTextRun(nsTextFrame::eInflated);
  gfxTextRun* textRun = aFrame->GetTextRun(nsTextFrame::eInflated);

  gfxTextRun::Range range = ConvertOriginalToSkipped(
      it, aFrame->GetContentOffset(), aFrame->GetContentLength());

  textRun->GetLineHeightMetrics(range, aAscent, aDescent);
}

/**
 * Updates an interval by intersecting it with another interval.
 * The intervals are specified using a start index and a length.
 */
static void IntersectInterval(uint32_t& aStart, uint32_t& aLength,
                              uint32_t aStartOther, uint32_t aLengthOther) {
  uint32_t aEnd = aStart + aLength;
  uint32_t aEndOther = aStartOther + aLengthOther;

  if (aStartOther >= aEnd || aStart >= aEndOther) {
    aLength = 0;
  } else {
    if (aStartOther >= aStart) aStart = aStartOther;
    aLength = std::min(aEnd, aEndOther) - aStart;
  }
}

/**
 * Intersects an interval as IntersectInterval does but by taking
 * the offset and length of the other interval from a
 * nsTextFrame::TrimmedOffsets object.
 */
static void TrimOffsets(uint32_t& aStart, uint32_t& aLength,
                        const nsTextFrame::TrimmedOffsets& aTrimmedOffsets) {
  IntersectInterval(aStart, aLength, aTrimmedOffsets.mStart,
                    aTrimmedOffsets.mLength);
}

/**
 * Returns the closest ancestor-or-self node that is not an SVG <a>
 * element.
 */
static nsIContent* GetFirstNonAAncestor(nsIContent* aContent) {
  while (aContent && aContent->IsSVGElement(nsGkAtoms::a)) {
    aContent = aContent->GetParent();
  }
  return aContent;
}

/**
 * Returns whether the given node is a text content element[1], taking into
 * account whether it has a valid parent.
 *
 * For example, in:
 *
 *   <svg xmlns="http://www.w3.org/2000/svg">
 *     <text><a/><text/></text>
 *     <tspan/>
 *   </svg>
 *
 * true would be returned for the outer <text> element and the <a> element,
 * and false for the inner <text> element (since a <text> is not allowed
 * to be a child of another <text>) and the <tspan> element (because it
 * must be inside a <text> subtree).
 *
 * Note that we don't support the <tref> element yet and this function
 * returns false for it.
 *
 * [1] https://svgwg.org/svg2-draft/intro.html#TermTextContentElement
 */
static bool IsTextContentElement(nsIContent* aContent) {
  if (aContent->IsSVGElement(nsGkAtoms::text)) {
    nsIContent* parent = GetFirstNonAAncestor(aContent->GetParent());
    return !parent || !IsTextContentElement(parent);
  }

  if (aContent->IsSVGElement(nsGkAtoms::textPath)) {
    nsIContent* parent = GetFirstNonAAncestor(aContent->GetParent());
    return parent && parent->IsSVGElement(nsGkAtoms::text);
  }

  return aContent->IsAnyOfSVGElements(nsGkAtoms::a, nsGkAtoms::tspan);
}

/**
 * Returns whether the specified frame is an nsTextFrame that has some text
 * content.
 */
static bool IsNonEmptyTextFrame(nsIFrame* aFrame) {
  nsTextFrame* textFrame = do_QueryFrame(aFrame);
  if (!textFrame) {
    return false;
  }

  return textFrame->GetContentLength() != 0;
}

/**
 * Takes an nsIFrame and if it is a text frame that has some text content,
 * returns it as an nsTextFrame and its corresponding Text.
 *
 * @param aFrame The frame to look at.
 * @param aTextFrame aFrame as an nsTextFrame (output).
 * @param aTextNode The Text content of aFrame (output).
 * @return true if aFrame is a non-empty text frame, false otherwise.
 */
static bool GetNonEmptyTextFrameAndNode(nsIFrame* aFrame,
                                        nsTextFrame*& aTextFrame,
                                        Text*& aTextNode) {
  nsTextFrame* text = do_QueryFrame(aFrame);
  bool isNonEmptyTextFrame = text && text->GetContentLength() != 0;

  if (isNonEmptyTextFrame) {
    nsIContent* content = text->GetContent();
    NS_ASSERTION(content && content->IsText(),
                 "unexpected content type for nsTextFrame");

    Text* node = content->AsText();
    MOZ_ASSERT(node->TextLength() != 0,
               "frame's GetContentLength() should be 0 if the text node "
               "has no content");

    aTextFrame = text;
    aTextNode = node;
  }

  MOZ_ASSERT(IsNonEmptyTextFrame(aFrame) == isNonEmptyTextFrame,
             "our logic should agree with IsNonEmptyTextFrame");
  return isNonEmptyTextFrame;
}

/**
 * Returns whether the specified atom is for one of the five
 * glyph positioning attributes that can appear on SVG text
 * elements -- x, y, dx, dy or rotate.
 */
static bool IsGlyphPositioningAttribute(nsAtom* aAttribute) {
  return aAttribute == nsGkAtoms::x || aAttribute == nsGkAtoms::y ||
         aAttribute == nsGkAtoms::dx || aAttribute == nsGkAtoms::dy ||
         aAttribute == nsGkAtoms::rotate;
}

/**
 * Returns the position in app units of a given baseline (using an
 * SVG dominant-baseline property value) for a given nsTextFrame.
 *
 * @param aFrame The text frame to inspect.
 * @param aTextRun The text run of aFrame.
 * @param aDominantBaseline The dominant-baseline value to use.
 */
static nscoord GetBaselinePosition(nsTextFrame* aFrame, gfxTextRun* aTextRun,
                                   StyleDominantBaseline aDominantBaseline,
                                   float aFontSizeScaleFactor) {
  WritingMode writingMode = aFrame->GetWritingMode();
  gfxFloat ascent, descent;
  aTextRun->GetLineHeightMetrics(ascent, descent);

  auto convertIfVerticalRL = [&](gfxFloat dominantBaseline) {
    return writingMode.IsVerticalRL() ? ascent + descent - dominantBaseline
                                      : dominantBaseline;
  };

  switch (aDominantBaseline) {
    case StyleDominantBaseline::Hanging:
      return convertIfVerticalRL(ascent * 0.2);
    case StyleDominantBaseline::TextBeforeEdge:
      return convertIfVerticalRL(0);

    case StyleDominantBaseline::Alphabetic:
      return writingMode.IsVerticalRL()
                 ? ascent * 0.5
                 : aFrame->GetLogicalBaseline(writingMode);

    case StyleDominantBaseline::Auto:
      return convertIfVerticalRL(aFrame->GetLogicalBaseline(writingMode));

    case StyleDominantBaseline::Middle:
      return convertIfVerticalRL(aFrame->GetLogicalBaseline(writingMode) -
                                 SVGContentUtils::GetFontXHeight(aFrame) / 2.0 *
                                     AppUnitsPerCSSPixel() *
                                     aFontSizeScaleFactor);

    case StyleDominantBaseline::TextAfterEdge:
    case StyleDominantBaseline::Ideographic:
      return writingMode.IsVerticalLR() ? 0 : ascent + descent;

    case StyleDominantBaseline::Central:
      return (ascent + descent) / 2.0;
    case StyleDominantBaseline::Mathematical:
      return convertIfVerticalRL(ascent / 2.0);
  }

  MOZ_ASSERT_UNREACHABLE("unexpected dominant-baseline value");
  return convertIfVerticalRL(aFrame->GetLogicalBaseline(writingMode));
}

/**
 * Truncates an array to be at most the length of another array.
 *
 * @param aArrayToTruncate The array to truncate.
 * @param aReferenceArray The array whose length will be used to truncate
 *   aArrayToTruncate to.
 */
template <typename T, typename U>
static void TruncateTo(nsTArray<T>& aArrayToTruncate,
                       const nsTArray<U>& aReferenceArray) {
  uint32_t length = aReferenceArray.Length();
  if (aArrayToTruncate.Length() > length) {
    aArrayToTruncate.TruncateLength(length);
  }
}

/**
 * Asserts that the anonymous block child of the SVGTextFrame has been
 * reflowed (or does not exist).  Returns null if the child has not been
 * reflowed, and the frame otherwise.
 *
 * We check whether the kid has been reflowed and not the frame itself
 * since we sometimes need to call this function during reflow, after the
 * kid has been reflowed but before we have cleared the dirty bits on the
 * frame itself.
 */
static SVGTextFrame* FrameIfAnonymousChildReflowed(SVGTextFrame* aFrame) {
  MOZ_ASSERT(aFrame, "aFrame must not be null");
  nsIFrame* kid = aFrame->PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    MOZ_ASSERT(false, "should have already reflowed the anonymous block child");
    return nullptr;
  }
  return aFrame;
}

static double GetContextScale(const gfxMatrix& aMatrix) {
  // The context scale is the ratio of the length of the transformed
  // diagonal vector (1,1) to the length of the untransformed diagonal
  // (which is sqrt(2)).
  gfxPoint p = aMatrix.TransformPoint(gfxPoint(1, 1)) -
               aMatrix.TransformPoint(gfxPoint(0, 0));
  return SVGContentUtils::ComputeNormalizedHypotenuse(p.x, p.y);
}

// ============================================================================
// Utility classes

// ----------------------------------------------------------------------------
// TextRenderedRun

/**
 * A run of text within a single nsTextFrame whose glyphs can all be painted
 * with a single call to nsTextFrame::PaintText.  A text rendered run can
 * be created for a sequence of two or more consecutive glyphs as long as:
 *
 *   - Only the first glyph has (or none of the glyphs have) been positioned
 *     with SVG text positioning attributes
 *   - All of the glyphs have zero rotation
 *   - The glyphs are not on a text path
 *   - The glyphs correspond to content within the one nsTextFrame
 *
 * A TextRenderedRunIterator produces TextRenderedRuns required for painting a
 * whole SVGTextFrame.
 */
struct TextRenderedRun {
  using Range = gfxTextRun::Range;

  /**
   * Constructs a TextRenderedRun that is uninitialized except for mFrame
   * being null.
   */
  TextRenderedRun() : mFrame(nullptr) {}

  /**
   * Constructs a TextRenderedRun with all of the information required to
   * paint it.  See the comments documenting the member variables below
   * for descriptions of the arguments.
   */
  TextRenderedRun(nsTextFrame* aFrame, const gfxPoint& aPosition,
                  float aLengthAdjustScaleFactor, double aRotate,
                  float aFontSizeScaleFactor, nscoord aBaseline,
                  uint32_t aTextFrameContentOffset,
                  uint32_t aTextFrameContentLength,
                  uint32_t aTextElementCharIndex)
      : mFrame(aFrame),
        mPosition(aPosition),
        mLengthAdjustScaleFactor(aLengthAdjustScaleFactor),
        mRotate(static_cast<float>(aRotate)),
        mFontSizeScaleFactor(aFontSizeScaleFactor),
        mBaseline(aBaseline),
        mTextFrameContentOffset(aTextFrameContentOffset),
        mTextFrameContentLength(aTextFrameContentLength),
        mTextElementCharIndex(aTextElementCharIndex) {}

  /**
   * Returns the text run for the text frame that this rendered run is part of.
   */
  gfxTextRun* GetTextRun() const {
    mFrame->EnsureTextRun(nsTextFrame::eInflated);
    return mFrame->GetTextRun(nsTextFrame::eInflated);
  }

  /**
   * Returns whether this rendered run is RTL.
   */
  bool IsRightToLeft() const { return GetTextRun()->IsRightToLeft(); }

  /**
   * Returns whether this rendered run is vertical.
   */
  bool IsVertical() const { return GetTextRun()->IsVertical(); }

  /**
   * Returns the transform that converts from a <text> element's user space into
   * the coordinate space that rendered runs can be painted directly in.
   *
   * The difference between this method and
   * GetTransformFromRunUserSpaceToUserSpace is that when calling in to
   * nsTextFrame::PaintText, it will already take into account any left clip
   * edge (that is, it doesn't just apply a visual clip to the rendered text, it
   * shifts the glyphs over so that they are painted with their left edge at the
   * x coordinate passed in to it). Thus we need to account for this in our
   * transform.
   *
   *
   * Assume that we have:
   *
   *   <text x="100" y="100" rotate="0 0 1 0 0 * 1">abcdef</text>.
   *
   * This would result in four text rendered runs:
   *
   *   - one for "ab"
   *   - one for "c"
   *   - one for "de"
   *   - one for "f"
   *
   * Assume now that we are painting the third TextRenderedRun.  It will have
   * a left clip edge that is the sum of the advances of "abc", and it will
   * have a right clip edge that is the advance of "f".  In
   * SVGTextFrame::PaintSVG(), we pass in nsPoint() (i.e., the origin)
   * as the point at which to paint the text frame, and we pass in the
   * clip edge values.  The nsTextFrame will paint the substring of its
   * text such that the top-left corner of the "d"'s glyph cell will be at
   * (0, 0) in the current coordinate system.
   *
   * Thus, GetTransformFromUserSpaceForPainting must return a transform from
   * whatever user space the <text> element is in to a coordinate space in
   * device pixels (as that's what nsTextFrame works in) where the origin is at
   * the same position as our user space mPositions[i].mPosition value for
   * the "d" glyph, which will be (100 + userSpaceAdvance("abc"), 100).
   * The translation required to do this (ignoring the scale to get from
   * user space to device pixels, and ignoring the
   * (100 + userSpaceAdvance("abc"), 100) translation) is:
   *
   *   (-leftEdge, -baseline)
   *
   * where baseline is the distance between the baseline of the text and the top
   * edge of the nsTextFrame.  We translate by -leftEdge horizontally because
   * the nsTextFrame will already shift the glyphs over by that amount and start
   * painting glyphs at x = 0.  We translate by -baseline vertically so that
   * painting the top edges of the glyphs at y = 0 will result in their
   * baselines being at our desired y position.
   *
   *
   * Now for an example with RTL text.  Assume our content is now
   * <text x="100" y="100" rotate="0 0 1 0 0 1">WERBEH</text>.  We'd have
   * the following text rendered runs:
   *
   *   - one for "EH"
   *   - one for "B"
   *   - one for "ER"
   *   - one for "W"
   *
   * Again, we are painting the third TextRenderedRun.  The left clip edge
   * is the advance of the "W" and the right clip edge is the sum of the
   * advances of "BEH".  Our translation to get the rendered "ER" glyphs
   * in the right place this time is:
   *
   *   (-frameWidth + rightEdge, -baseline)
   *
   * which is equivalent to:
   *
   *   (-(leftEdge + advance("ER")), -baseline)
   *
   * The reason we have to shift left additionally by the width of the run
   * of glyphs we are painting is that although the nsTextFrame is RTL,
   * we still supply the top-left corner to paint the frame at when calling
   * nsTextFrame::PaintText, even though our user space positions for each
   * glyph in mPositions specifies the origin of each glyph, which for RTL
   * glyphs is at the right edge of the glyph cell.
   *
   *
   * For any other use of an nsTextFrame in the context of a particular run
   * (such as hit testing, or getting its rectangle),
   * GetTransformFromRunUserSpaceToUserSpace should be used.
   *
   * @param aContext The context to use for unit conversions.
   */
  gfxMatrix GetTransformFromUserSpaceForPainting(
      nsPresContext* aContext, const nscoord aVisIStartEdge,
      const nscoord aVisIEndEdge) const;

  /**
   * Returns the transform that converts from "run user space" to a <text>
   * element's user space.  Run user space is a coordinate system that has the
   * same size as the <text>'s user space but rotated and translated such that
   * (0,0) is the top-left of the rectangle that bounds the text.
   *
   * @param aContext The context to use for unit conversions.
   */
  gfxMatrix GetTransformFromRunUserSpaceToUserSpace(
      nsPresContext* aContext) const;

  /**
   * Returns the transform that converts from "run user space" to float pixels
   * relative to the nsTextFrame that this rendered run is a part of.
   *
   * @param aContext The context to use for unit conversions.
   */
  gfxMatrix GetTransformFromRunUserSpaceToFrameUserSpace(
      nsPresContext* aContext) const;

  /**
   * Flag values used for the aFlags arguments of GetRunUserSpaceRect,
   * GetFrameUserSpaceRect and GetUserSpaceRect.
   */
  enum {
    // Includes the fill geometry of the text in the returned rectangle.
    eIncludeFill = 1,
    // Includes the stroke geometry of the text in the returned rectangle.
    eIncludeStroke = 2,
    // Don't include any horizontal glyph overflow in the returned rectangle.
    eNoHorizontalOverflow = 4
  };

  /**
   * Returns a rectangle that bounds the fill and/or stroke of the rendered run
   * in run user space.
   *
   * @param aContext The context to use for unit conversions.
   * @param aFlags A combination of the flags above (eIncludeFill and
   *   eIncludeStroke) indicating what parts of the text to include in
   *   the rectangle.
   */
  SVGBBox GetRunUserSpaceRect(nsPresContext* aContext, uint32_t aFlags) const;

  /**
   * Returns a rectangle that covers the fill and/or stroke of the rendered run
   * in "frame user space".
   *
   * Frame user space is a coordinate space of the same scale as the <text>
   * element's user space, but with its rotation set to the rotation of
   * the glyphs within this rendered run and its origin set to the position
   * such that placing the nsTextFrame there would result in the glyphs in
   * this rendered run being at their correct positions.
   *
   * For example, say we have <text x="100 150" y="100">ab</text>.  Assume
   * the advance of both the "a" and the "b" is 12 user units, and the
   * ascent of the text is 8 user units and its descent is 6 user units,
   * and that we are not measuing the stroke of the text, so that we stay
   * entirely within the glyph cells.
   *
   * There will be two text rendered runs, one for "a" and one for "b".
   *
   * The frame user space for the "a" run will have its origin at
   * (100, 100 - 8) in the <text> element's user space and will have its
   * axes aligned with the user space (since there is no rotate="" or
   * text path involve) and with its scale the same as the user space.
   * The rect returned by this method will be (0, 0, 12, 14), since the "a"
   * glyph is right at the left of the nsTextFrame.
   *
   * The frame user space for the "b" run will have its origin at
   * (150 - 12, 100 - 8), and scale/rotation the same as above.  The rect
   * returned by this method will be (12, 0, 12, 14), since we are
   * advance("a") horizontally in to the text frame.
   *
   * @param aContext The context to use for unit conversions.
   * @param aFlags A combination of the flags above (eIncludeFill and
   *   eIncludeStroke) indicating what parts of the text to include in
   *   the rectangle.
   */
  SVGBBox GetFrameUserSpaceRect(nsPresContext* aContext, uint32_t aFlags) const;

  /**
   * Returns a rectangle that covers the fill and/or stroke of the rendered run
   * in the <text> element's user space.
   *
   * @param aContext The context to use for unit conversions.
   * @param aFlags A combination of the flags above indicating what parts of
   *   the text to include in the rectangle.
   * @param aAdditionalTransform An additional transform to apply to the
   *   frame user space rectangle before its bounds are transformed into
   *   user space.
   */
  SVGBBox GetUserSpaceRect(
      nsPresContext* aContext, uint32_t aFlags,
      const gfxMatrix* aAdditionalTransform = nullptr) const;

  /**
   * Gets the app unit amounts to clip from the left and right edges of
   * the nsTextFrame in order to paint just this rendered run.
   *
   * Note that if clip edge amounts land in the middle of a glyph, the
   * glyph won't be painted at all.  The clip edges are thus more of
   * a selection mechanism for which glyphs will be painted, rather
   * than a geometric clip.
   */
  void GetClipEdges(nscoord& aVisIStartEdge, nscoord& aVisIEndEdge) const;

  /**
   * Returns the advance width of the whole rendered run.
   */
  nscoord GetAdvanceWidth() const;

  /**
   * Returns the index of the character into this rendered run whose
   * glyph cell contains the given point, or -1 if there is no such
   * character.  This does not hit test against any overflow.
   *
   * @param aContext The context to use for unit conversions.
   * @param aPoint The point in the user space of the <text> element.
   */
  int32_t GetCharNumAtPosition(nsPresContext* aContext,
                               const gfxPoint& aPoint) const;

  /**
   * The text frame that this rendered run lies within.
   */
  nsTextFrame* mFrame;

  /**
   * The point in user space that the text is positioned at.
   *
   * For a horizontal run:
   * The x coordinate is the left edge of a LTR run of text or the right edge of
   * an RTL run.  The y coordinate is the baseline of the text.
   * For a vertical run:
   * The x coordinate is the baseline of the text.
   * The y coordinate is the top edge of a LTR run, or bottom of RTL.
   */
  gfxPoint mPosition;

  /**
   * The horizontal scale factor to apply when painting glyphs to take
   * into account textLength="".
   */
  float mLengthAdjustScaleFactor;

  /**
   * The rotation in radians in the user coordinate system that the text has.
   */
  float mRotate;

  /**
   * The scale factor that was used to transform the text run's original font
   * size into a sane range for painting and measurement.
   */
  double mFontSizeScaleFactor;

  /**
   * The baseline in app units of this text run.  The measurement is from the
   * top of the text frame. (From the left edge if vertical.)
   */
  nscoord mBaseline;

  /**
   * The offset and length in mFrame's content Text that corresponds to
   * this text rendered run.  These are original char indexes.
   */
  uint32_t mTextFrameContentOffset;
  uint32_t mTextFrameContentLength;

  /**
   * The character index in the whole SVG <text> element that this text rendered
   * run begins at.
   */
  uint32_t mTextElementCharIndex;
};

gfxMatrix TextRenderedRun::GetTransformFromUserSpaceForPainting(
    nsPresContext* aContext, const nscoord aVisIStartEdge,
    const nscoord aVisIEndEdge) const {
  // We transform to device pixels positioned such that painting the text frame
  // at (0,0) with aItem will result in the text being in the right place.

  gfxMatrix m;
  if (!mFrame) {
    return m;
  }

  float cssPxPerDevPx =
      nsPresContext::AppUnitsToFloatCSSPixels(aContext->AppUnitsPerDevPixel());

  // Glyph position in user space.
  m.PreTranslate(mPosition / cssPxPerDevPx);

  // Take into account any font size scaling and scaling due to textLength="".
  m.PreScale(1.0 / mFontSizeScaleFactor, 1.0 / mFontSizeScaleFactor);

  // Rotation due to rotate="" or a <textPath>.
  m.PreRotate(mRotate);

  m.PreScale(mLengthAdjustScaleFactor, 1.0);

  // Translation to get the text frame in the right place.
  nsPoint t;

  if (IsVertical()) {
    t = nsPoint(-mBaseline, IsRightToLeft()
                                ? -mFrame->GetRect().height + aVisIEndEdge
                                : -aVisIStartEdge);
  } else {
    t = nsPoint(IsRightToLeft() ? -mFrame->GetRect().width + aVisIEndEdge
                                : -aVisIStartEdge,
                -mBaseline);
  }
  m.PreTranslate(AppUnitsToGfxUnits(t, aContext));

  return m;
}

gfxMatrix TextRenderedRun::GetTransformFromRunUserSpaceToUserSpace(
    nsPresContext* aContext) const {
  gfxMatrix m;
  if (!mFrame) {
    return m;
  }

  float cssPxPerDevPx =
      nsPresContext::AppUnitsToFloatCSSPixels(aContext->AppUnitsPerDevPixel());

  nscoord start, end;
  GetClipEdges(start, end);

  // Glyph position in user space.
  m.PreTranslate(mPosition);

  // Rotation due to rotate="" or a <textPath>.
  m.PreRotate(mRotate);

  // Scale due to textLength="".
  m.PreScale(mLengthAdjustScaleFactor, 1.0);

  // Translation to get the text frame in the right place.
  nsPoint t;
  if (IsVertical()) {
    t = nsPoint(-mBaseline,
                IsRightToLeft() ? -mFrame->GetRect().height + start + end : 0);
  } else {
    t = nsPoint(IsRightToLeft() ? -mFrame->GetRect().width + start + end : 0,
                -mBaseline);
  }
  m.PreTranslate(AppUnitsToGfxUnits(t, aContext) * cssPxPerDevPx /
                 mFontSizeScaleFactor);

  return m;
}

gfxMatrix TextRenderedRun::GetTransformFromRunUserSpaceToFrameUserSpace(
    nsPresContext* aContext) const {
  gfxMatrix m;
  if (!mFrame) {
    return m;
  }

  nscoord start, end;
  GetClipEdges(start, end);

  // Translate by the horizontal distance into the text frame this
  // rendered run is.
  gfxFloat appPerCssPx = AppUnitsPerCSSPixel();
  gfxPoint t = IsVertical() ? gfxPoint(0, start / appPerCssPx)
                            : gfxPoint(start / appPerCssPx, 0);
  return m.PreTranslate(t);
}

SVGBBox TextRenderedRun::GetRunUserSpaceRect(nsPresContext* aContext,
                                             uint32_t aFlags) const {
  SVGBBox r;
  if (!mFrame) {
    return r;
  }

  // Determine the amount of overflow above and below the frame's mRect.
  //
  // We need to call InkOverflowRectRelativeToSelf because this includes
  // overflowing decorations, which the MeasureText call below does not.  We
  // assume here the decorations only overflow above and below the frame, never
  // horizontally.
  nsRect self = mFrame->InkOverflowRectRelativeToSelf();
  nsRect rect = mFrame->GetRect();
  bool vertical = IsVertical();
  nscoord above = vertical ? -self.x : -self.y;
  nscoord below =
      vertical ? self.XMost() - rect.width : self.YMost() - rect.height;

  gfxSkipCharsIterator it = mFrame->EnsureTextRun(nsTextFrame::eInflated);
  gfxSkipCharsIterator start = it;
  gfxTextRun* textRun = mFrame->GetTextRun(nsTextFrame::eInflated);

  // Get the content range for this rendered run.
  Range range = ConvertOriginalToSkipped(it, mTextFrameContentOffset,
                                         mTextFrameContentLength);
  if (range.Length() == 0) {
    return r;
  }

  // FIXME(heycam): We could create a single PropertyProvider for all
  // TextRenderedRuns that correspond to the text frame, rather than recreate
  // it each time here.
  nsTextFrame::PropertyProvider provider(mFrame, start);

  // Measure that range.
  gfxTextRun::Metrics metrics = textRun->MeasureText(
      range, gfxFont::LOOSE_INK_EXTENTS, nullptr, &provider);
  // Make sure it includes the font-box.
  gfxRect fontBox(0, -metrics.mAscent, metrics.mAdvanceWidth,
                  metrics.mAscent + metrics.mDescent);
  metrics.mBoundingBox.UnionRect(metrics.mBoundingBox, fontBox);

  // Determine the rectangle that covers the rendered run's fill,
  // taking into account the measured vertical overflow due to
  // decorations.
  nscoord baseline =
      NSToCoordRoundWithClamp(metrics.mBoundingBox.y + metrics.mAscent);
  gfxFloat x, width;
  if (aFlags & eNoHorizontalOverflow) {
    x = 0.0;
    width = textRun->GetAdvanceWidth(range, &provider);
    if (width < 0.0) {
      x = width;
      width = -width;
    }
  } else {
    x = metrics.mBoundingBox.x;
    width = metrics.mBoundingBox.width;
  }
  nsRect fillInAppUnits(
      NSToCoordRoundWithClamp(x), baseline - above,
      NSToCoordRoundWithClamp(width),
      NSToCoordRoundWithClamp(metrics.mBoundingBox.height) + above + below);
  if (textRun->IsVertical()) {
    // Swap line-relative textMetrics dimensions to physical coordinates.
    std::swap(fillInAppUnits.x, fillInAppUnits.y);
    std::swap(fillInAppUnits.width, fillInAppUnits.height);
  }

  // Convert the app units rectangle to user units.
  gfxRect fill = AppUnitsToFloatCSSPixels(
      gfxRect(fillInAppUnits.x, fillInAppUnits.y, fillInAppUnits.width,
              fillInAppUnits.height),
      aContext);

  // Scale the rectangle up due to any mFontSizeScaleFactor.
  fill.Scale(1.0 / mFontSizeScaleFactor);

  // Include the fill if requested.
  if (aFlags & eIncludeFill) {
    r = fill;
  }

  // Include the stroke if requested.
  if ((aFlags & eIncludeStroke) && !fill.IsEmpty() &&
      SVGUtils::GetStrokeWidth(mFrame) > 0) {
    r.UnionEdges(
        SVGUtils::PathExtentsToMaxStrokeExtents(fill, mFrame, gfxMatrix()));
  }

  return r;
}

SVGBBox TextRenderedRun::GetFrameUserSpaceRect(nsPresContext* aContext,
                                               uint32_t aFlags) const {
  SVGBBox r = GetRunUserSpaceRect(aContext, aFlags);
  if (r.IsEmpty()) {
    return r;
  }
  gfxMatrix m = GetTransformFromRunUserSpaceToFrameUserSpace(aContext);
  return m.TransformBounds(r.ToThebesRect());
}

SVGBBox TextRenderedRun::GetUserSpaceRect(
    nsPresContext* aContext, uint32_t aFlags,
    const gfxMatrix* aAdditionalTransform) const {
  SVGBBox r = GetRunUserSpaceRect(aContext, aFlags);
  if (r.IsEmpty()) {
    return r;
  }
  gfxMatrix m = GetTransformFromRunUserSpaceToUserSpace(aContext);
  if (aAdditionalTransform) {
    m *= *aAdditionalTransform;
  }
  return m.TransformBounds(r.ToThebesRect());
}

void TextRenderedRun::GetClipEdges(nscoord& aVisIStartEdge,
                                   nscoord& aVisIEndEdge) const {
  uint32_t contentLength = mFrame->GetContentLength();
  if (mTextFrameContentOffset == 0 &&
      mTextFrameContentLength == contentLength) {
    // If the rendered run covers the entire content, we know we don't need
    // to clip without having to measure anything.
    aVisIStartEdge = 0;
    aVisIEndEdge = 0;
    return;
  }

  gfxSkipCharsIterator it = mFrame->EnsureTextRun(nsTextFrame::eInflated);
  gfxTextRun* textRun = mFrame->GetTextRun(nsTextFrame::eInflated);
  nsTextFrame::PropertyProvider provider(mFrame, it);

  // Get the covered content offset/length for this rendered run in skipped
  // characters, since that is what GetAdvanceWidth expects.
  Range runRange = ConvertOriginalToSkipped(it, mTextFrameContentOffset,
                                            mTextFrameContentLength);

  // Get the offset/length of the whole nsTextFrame.
  uint32_t frameOffset = mFrame->GetContentOffset();
  uint32_t frameLength = mFrame->GetContentLength();

  // Trim the whole-nsTextFrame offset/length to remove any leading/trailing
  // white space, as the nsTextFrame when painting does not include them when
  // interpreting clip edges.
  nsTextFrame::TrimmedOffsets trimmedOffsets =
      mFrame->GetTrimmedOffsets(mFrame->TextFragment());
  TrimOffsets(frameOffset, frameLength, trimmedOffsets);

  // Convert the trimmed whole-nsTextFrame offset/length into skipped
  // characters.
  Range frameRange = ConvertOriginalToSkipped(it, frameOffset, frameLength);

  // Measure the advance width in the text run between the start of
  // frame's content and the start of the rendered run's content,
  nscoord startEdge = textRun->GetAdvanceWidth(
      Range(frameRange.start, runRange.start), &provider);

  // and between the end of the rendered run's content and the end
  // of the frame's content.
  nscoord endEdge =
      textRun->GetAdvanceWidth(Range(runRange.end, frameRange.end), &provider);

  if (textRun->IsRightToLeft()) {
    aVisIStartEdge = endEdge;
    aVisIEndEdge = startEdge;
  } else {
    aVisIStartEdge = startEdge;
    aVisIEndEdge = endEdge;
  }
}

nscoord TextRenderedRun::GetAdvanceWidth() const {
  gfxSkipCharsIterator it = mFrame->EnsureTextRun(nsTextFrame::eInflated);
  gfxTextRun* textRun = mFrame->GetTextRun(nsTextFrame::eInflated);
  nsTextFrame::PropertyProvider provider(mFrame, it);

  Range range = ConvertOriginalToSkipped(it, mTextFrameContentOffset,
                                         mTextFrameContentLength);

  return textRun->GetAdvanceWidth(range, &provider);
}

int32_t TextRenderedRun::GetCharNumAtPosition(nsPresContext* aContext,
                                              const gfxPoint& aPoint) const {
  if (mTextFrameContentLength == 0) {
    return -1;
  }

  float cssPxPerDevPx =
      nsPresContext::AppUnitsToFloatCSSPixels(aContext->AppUnitsPerDevPixel());

  // Convert the point from user space into run user space, and take
  // into account any mFontSizeScaleFactor.
  gfxMatrix m = GetTransformFromRunUserSpaceToUserSpace(aContext);
  if (!m.Invert()) {
    return -1;
  }
  gfxPoint p = m.TransformPoint(aPoint) / cssPxPerDevPx * mFontSizeScaleFactor;

  // First check that the point lies vertically between the top and bottom
  // edges of the text.
  gfxFloat ascent, descent;
  GetAscentAndDescentInAppUnits(mFrame, ascent, descent);

  WritingMode writingMode = mFrame->GetWritingMode();
  if (writingMode.IsVertical()) {
    gfxFloat leftEdge = mFrame->GetLogicalBaseline(writingMode) -
                        (writingMode.IsVerticalRL() ? ascent : descent);
    gfxFloat rightEdge = leftEdge + ascent + descent;
    if (p.x < aContext->AppUnitsToGfxUnits(leftEdge) ||
        p.x > aContext->AppUnitsToGfxUnits(rightEdge)) {
      return -1;
    }
  } else {
    gfxFloat topEdge = mFrame->GetLogicalBaseline(writingMode) - ascent;
    gfxFloat bottomEdge = topEdge + ascent + descent;
    if (p.y < aContext->AppUnitsToGfxUnits(topEdge) ||
        p.y > aContext->AppUnitsToGfxUnits(bottomEdge)) {
      return -1;
    }
  }

  gfxSkipCharsIterator it = mFrame->EnsureTextRun(nsTextFrame::eInflated);
  gfxTextRun* textRun = mFrame->GetTextRun(nsTextFrame::eInflated);
  nsTextFrame::PropertyProvider provider(mFrame, it);

  // Next check that the point lies horizontally within the left and right
  // edges of the text.
  Range range = ConvertOriginalToSkipped(it, mTextFrameContentOffset,
                                         mTextFrameContentLength);
  gfxFloat runAdvance =
      aContext->AppUnitsToGfxUnits(textRun->GetAdvanceWidth(range, &provider));

  gfxFloat pos = writingMode.IsVertical() ? p.y : p.x;
  if (pos < 0 || pos >= runAdvance) {
    return -1;
  }

  // Finally, measure progressively smaller portions of the rendered run to
  // find which glyph it lies within.  This will need to change once we
  // support letter-spacing and word-spacing.
  bool rtl = textRun->IsRightToLeft();
  for (int32_t i = mTextFrameContentLength - 1; i >= 0; i--) {
    range = ConvertOriginalToSkipped(it, mTextFrameContentOffset, i);
    gfxFloat advance = aContext->AppUnitsToGfxUnits(
        textRun->GetAdvanceWidth(range, &provider));
    if ((rtl && pos < runAdvance - advance) || (!rtl && pos >= advance)) {
      return i;
    }
  }
  return -1;
}

// ----------------------------------------------------------------------------
// TextNodeIterator

enum SubtreePosition { eBeforeSubtree, eWithinSubtree, eAfterSubtree };

/**
 * An iterator class for Text that are descendants of a given node, the
 * root.  Nodes are iterated in document order.  An optional subtree can be
 * specified, in which case the iterator will track whether the current state of
 * the traversal over the tree is within that subtree or is past that subtree.
 */
class TextNodeIterator {
 public:
  /**
   * Constructs a TextNodeIterator with the specified root node and optional
   * subtree.
   */
  explicit TextNodeIterator(nsIContent* aRoot, nsIContent* aSubtree = nullptr)
      : mRoot(aRoot),
        mSubtree(aSubtree == aRoot ? nullptr : aSubtree),
        mCurrent(aRoot),
        mSubtreePosition(mSubtree ? eBeforeSubtree : eWithinSubtree) {
    NS_ASSERTION(aRoot, "expected non-null root");
    if (!aRoot->IsText()) {
      Next();
    }
  }

  /**
   * Returns the current Text, or null if the iterator has finished.
   */
  Text* Current() const { return mCurrent ? mCurrent->AsText() : nullptr; }

  /**
   * Advances to the next Text and returns it, or null if the end of
   * iteration has been reached.
   */
  Text* Next();

  /**
   * Returns whether the iterator is currently within the subtree rooted
   * at mSubtree.  Returns true if we are not tracking a subtree (we consider
   * that we're always within the subtree).
   */
  bool IsWithinSubtree() const { return mSubtreePosition == eWithinSubtree; }

  /**
   * Returns whether the iterator is past the subtree rooted at mSubtree.
   * Returns false if we are not tracking a subtree.
   */
  bool IsAfterSubtree() const { return mSubtreePosition == eAfterSubtree; }

 private:
  /**
   * The root under which all Text will be iterated over.
   */
  nsIContent* const mRoot;

  /**
   * The node rooting the subtree to track.
   */
  nsIContent* const mSubtree;

  /**
   * The current node during iteration.
   */
  nsIContent* mCurrent;

  /**
   * The current iterator position relative to mSubtree.
   */
  SubtreePosition mSubtreePosition;
};

Text* TextNodeIterator::Next() {
  // Starting from mCurrent, we do a non-recursive traversal to the next
  // Text beneath mRoot, updating mSubtreePosition appropriately if we
  // encounter mSubtree.
  if (mCurrent) {
    do {
      nsIContent* next =
          IsTextContentElement(mCurrent) ? mCurrent->GetFirstChild() : nullptr;
      if (next) {
        mCurrent = next;
        if (mCurrent == mSubtree) {
          mSubtreePosition = eWithinSubtree;
        }
      } else {
        for (;;) {
          if (mCurrent == mRoot) {
            mCurrent = nullptr;
            break;
          }
          if (mCurrent == mSubtree) {
            mSubtreePosition = eAfterSubtree;
          }
          next = mCurrent->GetNextSibling();
          if (next) {
            mCurrent = next;
            if (mCurrent == mSubtree) {
              mSubtreePosition = eWithinSubtree;
            }
            break;
          }
          if (mCurrent == mSubtree) {
            mSubtreePosition = eAfterSubtree;
          }
          mCurrent = mCurrent->GetParent();
        }
      }
    } while (mCurrent && !mCurrent->IsText());
  }

  return mCurrent ? mCurrent->AsText() : nullptr;
}

// ----------------------------------------------------------------------------
// TextNodeCorrespondenceRecorder

/**
 * TextNodeCorrespondence is used as the value of a frame property that
 * is stored on all its descendant nsTextFrames.  It stores the number of DOM
 * characters between it and the previous nsTextFrame that did not have an
 * nsTextFrame created for them, due to either not being in a correctly
 * parented text content element, or because they were display:none.
 * These are called "undisplayed characters".
 *
 * See also TextNodeCorrespondenceRecorder below, which is what sets the
 * frame property.
 */
struct TextNodeCorrespondence {
  explicit TextNodeCorrespondence(uint32_t aUndisplayedCharacters)
      : mUndisplayedCharacters(aUndisplayedCharacters) {}

  uint32_t mUndisplayedCharacters;
};

NS_DECLARE_FRAME_PROPERTY_DELETABLE(TextNodeCorrespondenceProperty,
                                    TextNodeCorrespondence)

/**
 * Returns the number of undisplayed characters before the specified
 * nsTextFrame.
 */
static uint32_t GetUndisplayedCharactersBeforeFrame(nsTextFrame* aFrame) {
  void* value = aFrame->GetProperty(TextNodeCorrespondenceProperty());
  TextNodeCorrespondence* correspondence =
      static_cast<TextNodeCorrespondence*>(value);
  if (!correspondence) {
    // FIXME bug 903785
    NS_ERROR(
        "expected a TextNodeCorrespondenceProperty on nsTextFrame "
        "used for SVG text");
    return 0;
  }
  return correspondence->mUndisplayedCharacters;
}

/**
 * Traverses the nsTextFrames for an SVGTextFrame and records a
 * TextNodeCorrespondenceProperty on each for the number of undisplayed DOM
 * characters between each frame.  This is done by iterating simultaneously
 * over the Text and nsTextFrames and noting when Text (or
 * parts of them) are skipped when finding the next nsTextFrame.
 */
class TextNodeCorrespondenceRecorder {
 public:
  /**
   * Entry point for the TextNodeCorrespondenceProperty recording.
   */
  static void RecordCorrespondence(SVGTextFrame* aRoot);

 private:
  explicit TextNodeCorrespondenceRecorder(SVGTextFrame* aRoot)
      : mNodeIterator(aRoot->GetContent()),
        mPreviousNode(nullptr),
        mNodeCharIndex(0) {}

  void Record(SVGTextFrame* aRoot);
  void TraverseAndRecord(nsIFrame* aFrame);

  /**
   * Returns the next non-empty Text.
   */
  Text* NextNode();

  /**
   * The iterator over the Text that we use as we simultaneously
   * iterate over the nsTextFrames.
   */
  TextNodeIterator mNodeIterator;

  /**
   * The previous Text we iterated over.
   */
  Text* mPreviousNode;

  /**
   * The index into the current Text's character content.
   */
  uint32_t mNodeCharIndex;
};

/* static */
void TextNodeCorrespondenceRecorder::RecordCorrespondence(SVGTextFrame* aRoot) {
  if (aRoot->HasAnyStateBits(NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY)) {
    // Resolve bidi so that continuation frames are created if necessary:
    aRoot->MaybeResolveBidiForAnonymousBlockChild();
    TextNodeCorrespondenceRecorder recorder(aRoot);
    recorder.Record(aRoot);
    aRoot->RemoveStateBits(NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY);
  }
}

void TextNodeCorrespondenceRecorder::Record(SVGTextFrame* aRoot) {
  if (!mNodeIterator.Current()) {
    // If there are no Text nodes then there is nothing to do.
    return;
  }

  // Traverse over all the nsTextFrames and record the number of undisplayed
  // characters.
  TraverseAndRecord(aRoot);

  // Find how many undisplayed characters there are after the final nsTextFrame.
  uint32_t undisplayed = 0;
  if (mNodeIterator.Current()) {
    if (mPreviousNode && mPreviousNode->TextLength() != mNodeCharIndex) {
      // The last nsTextFrame ended part way through a Text node.  The
      // remaining characters count as undisplayed.
      NS_ASSERTION(mNodeCharIndex < mPreviousNode->TextLength(),
                   "incorrect tracking of undisplayed characters in "
                   "text nodes");
      undisplayed += mPreviousNode->TextLength() - mNodeCharIndex;
    }
    // All the remaining Text that we iterate must also be undisplayed.
    for (Text* textNode = mNodeIterator.Current(); textNode;
         textNode = NextNode()) {
      undisplayed += textNode->TextLength();
    }
  }

  // Record the trailing number of undisplayed characters on the
  // SVGTextFrame.
  aRoot->mTrailingUndisplayedCharacters = undisplayed;
}

Text* TextNodeCorrespondenceRecorder::NextNode() {
  mPreviousNode = mNodeIterator.Current();
  Text* next;
  do {
    next = mNodeIterator.Next();
  } while (next && next->TextLength() == 0);
  return next;
}

void TextNodeCorrespondenceRecorder::TraverseAndRecord(nsIFrame* aFrame) {
  // Recursively iterate over the frame tree, for frames that correspond
  // to text content elements.
  if (IsTextContentElement(aFrame->GetContent())) {
    for (nsIFrame* f : aFrame->PrincipalChildList()) {
      TraverseAndRecord(f);
    }
    return;
  }

  nsTextFrame* frame;  // The current text frame.
  Text* node;          // The text node for the current text frame.
  if (!GetNonEmptyTextFrameAndNode(aFrame, frame, node)) {
    // If this isn't an nsTextFrame, or is empty, nothing to do.
    return;
  }

  NS_ASSERTION(frame->GetContentOffset() >= 0,
               "don't know how to handle negative content indexes");

  uint32_t undisplayed = 0;
  if (!mPreviousNode) {
    // Must be the very first text frame.
    NS_ASSERTION(mNodeCharIndex == 0,
                 "incorrect tracking of undisplayed "
                 "characters in text nodes");
    if (!mNodeIterator.Current()) {
      MOZ_ASSERT_UNREACHABLE(
          "incorrect tracking of correspondence between "
          "text frames and text nodes");
    } else {
      // Each whole Text we find before we get to the text node for the
      // first text frame must be undisplayed.
      while (mNodeIterator.Current() != node) {
        undisplayed += mNodeIterator.Current()->TextLength();
        NextNode();
      }
      // If the first text frame starts at a non-zero content offset, then those
      // earlier characters are also undisplayed.
      undisplayed += frame->GetContentOffset();
      NextNode();
    }
  } else if (mPreviousNode == node) {
    // Same text node as last time.
    if (static_cast<uint32_t>(frame->GetContentOffset()) != mNodeCharIndex) {
      // We have some characters in the middle of the text node
      // that are undisplayed.
      NS_ASSERTION(
          mNodeCharIndex < static_cast<uint32_t>(frame->GetContentOffset()),
          "incorrect tracking of undisplayed characters in "
          "text nodes");
      undisplayed = frame->GetContentOffset() - mNodeCharIndex;
    }
  } else {
    // Different text node from last time.
    if (mPreviousNode->TextLength() != mNodeCharIndex) {
      NS_ASSERTION(mNodeCharIndex < mPreviousNode->TextLength(),
                   "incorrect tracking of undisplayed characters in "
                   "text nodes");
      // Any trailing characters at the end of the previous Text are
      // undisplayed.
      undisplayed = mPreviousNode->TextLength() - mNodeCharIndex;
    }
    // Each whole Text we find before we get to the text node for
    // the current text frame must be undisplayed.
    while (mNodeIterator.Current() && mNodeIterator.Current() != node) {
      undisplayed += mNodeIterator.Current()->TextLength();
      NextNode();
    }
    // If the current text frame starts at a non-zero content offset, then those
    // earlier characters are also undisplayed.
    undisplayed += frame->GetContentOffset();
    NextNode();
  }

  // Set the frame property.
  frame->SetProperty(TextNodeCorrespondenceProperty(),
                     new TextNodeCorrespondence(undisplayed));

  // Remember how far into the current Text we are.
  mNodeCharIndex = frame->GetContentEnd();
}

// ----------------------------------------------------------------------------
// TextFrameIterator

/**
 * An iterator class for nsTextFrames that are descendants of an
 * SVGTextFrame.  The iterator can optionally track whether the
 * current nsTextFrame is for a descendant of, or past, a given subtree
 * content node or frame.  (This functionality is used for example by the SVG
 * DOM text methods to get only the nsTextFrames for a particular <tspan>.)
 *
 * TextFrameIterator also tracks and exposes other information about the
 * current nsTextFrame:
 *
 *   * how many undisplayed characters came just before it
 *   * its position (in app units) relative to the SVGTextFrame's anonymous
 *     block frame
 *   * what nsInlineFrame corresponding to a <textPath> element it is a
 *     descendant of
 *   * what computed dominant-baseline value applies to it
 *
 * Note that any text frames that are empty -- whose ContentLength() is 0 --
 * will be skipped over.
 */
class MOZ_STACK_CLASS TextFrameIterator {
 public:
  /**
   * Constructs a TextFrameIterator for the specified SVGTextFrame
   * with an optional frame subtree to restrict iterated text frames to.
   */
  explicit TextFrameIterator(SVGTextFrame* aRoot,
                             const nsIFrame* aSubtree = nullptr)
      : mRootFrame(aRoot),
        mSubtree(aSubtree),
        mCurrentFrame(aRoot),
        mCurrentPosition(),
        mSubtreePosition(mSubtree ? eBeforeSubtree : eWithinSubtree) {
    Init();
  }

  /**
   * Constructs a TextFrameIterator for the specified SVGTextFrame
   * with an optional frame content subtree to restrict iterated text frames to.
   */
  TextFrameIterator(SVGTextFrame* aRoot, nsIContent* aSubtree)
      : mRootFrame(aRoot),
        mSubtree(aRoot && aSubtree && aSubtree != aRoot->GetContent()
                     ? aSubtree->GetPrimaryFrame()
                     : nullptr),
        mCurrentFrame(aRoot),
        mCurrentPosition(),
        mSubtreePosition(mSubtree ? eBeforeSubtree : eWithinSubtree) {
    Init();
  }

  /**
   * Returns the root SVGTextFrame this TextFrameIterator is iterating over.
   */
  SVGTextFrame* Root() const { return mRootFrame; }

  /**
   * Returns the current nsTextFrame.
   */
  nsTextFrame* Current() const { return do_QueryFrame(mCurrentFrame); }

  /**
   * Returns the number of undisplayed characters in the DOM just before the
   * current frame.
   */
  uint32_t UndisplayedCharacters() const;

  /**
   * Returns the current frame's position, in app units, relative to the
   * root SVGTextFrame's anonymous block frame.
   */
  nsPoint Position() const { return mCurrentPosition; }

  /**
   * Advances to the next nsTextFrame and returns it.
   */
  nsTextFrame* Next();

  /**
   * Returns whether the iterator is within the subtree.
   */
  bool IsWithinSubtree() const { return mSubtreePosition == eWithinSubtree; }

  /**
   * Returns whether the iterator is past the subtree.
   */
  bool IsAfterSubtree() const { return mSubtreePosition == eAfterSubtree; }

  /**
   * Returns the frame corresponding to the <textPath> element, if we
   * are inside one.
   */
  nsIFrame* TextPathFrame() const {
    return mTextPathFrames.IsEmpty()
               ? nullptr
               : mTextPathFrames.ElementAt(mTextPathFrames.Length() - 1);
  }

  /**
   * Returns the current frame's computed dominant-baseline value.
   */
  StyleDominantBaseline DominantBaseline() const {
    return mBaselines.ElementAt(mBaselines.Length() - 1);
  }

  /**
   * Finishes the iterator.
   */
  void Close() { mCurrentFrame = nullptr; }

 private:
  /**
   * Initializes the iterator and advances to the first item.
   */
  void Init() {
    if (!mRootFrame) {
      return;
    }

    mBaselines.AppendElement(mRootFrame->StyleSVG()->mDominantBaseline);
    Next();
  }

  /**
   * Pushes the specified frame's computed dominant-baseline value.
   * If the value of the property is "auto", then the parent frame's
   * computed value is used.
   */
  void PushBaseline(nsIFrame* aNextFrame);

  /**
   * Pops the current dominant-baseline off the stack.
   */
  void PopBaseline();

  /**
   * The root frame we are iterating through.
   */
  SVGTextFrame* const mRootFrame;

  /**
   * The frame for the subtree we are also interested in tracking.
   */
  const nsIFrame* const mSubtree;

  /**
   * The current value of the iterator.
   */
  nsIFrame* mCurrentFrame;

  /**
   * The position, in app units, of the current frame relative to mRootFrame.
   */
  nsPoint mCurrentPosition;

  /**
   * Stack of frames corresponding to <textPath> elements that are in scope
   * for the current frame.
   */
  AutoTArray<nsIFrame*, 1> mTextPathFrames;

  /**
   * Stack of dominant-baseline values to record as we traverse through the
   * frame tree.
   */
  AutoTArray<StyleDominantBaseline, 8> mBaselines;

  /**
   * The iterator's current position relative to mSubtree.
   */
  SubtreePosition mSubtreePosition;
};

uint32_t TextFrameIterator::UndisplayedCharacters() const {
  MOZ_ASSERT(
      !mRootFrame->HasAnyStateBits(NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY),
      "Text correspondence must be up to date");

  if (!mCurrentFrame) {
    return mRootFrame->mTrailingUndisplayedCharacters;
  }

  nsTextFrame* frame = do_QueryFrame(mCurrentFrame);
  return GetUndisplayedCharactersBeforeFrame(frame);
}

nsTextFrame* TextFrameIterator::Next() {
  // Starting from mCurrentFrame, we do a non-recursive traversal to the next
  // nsTextFrame beneath mRoot, updating mSubtreePosition appropriately if we
  // encounter mSubtree.
  if (mCurrentFrame) {
    do {
      nsIFrame* next = IsTextContentElement(mCurrentFrame->GetContent())
                           ? mCurrentFrame->PrincipalChildList().FirstChild()
                           : nullptr;
      if (next) {
        // Descend into this frame, and accumulate its position.
        mCurrentPosition += next->GetPosition();
        if (next->GetContent()->IsSVGElement(nsGkAtoms::textPath)) {
          // Record this <textPath> frame.
          mTextPathFrames.AppendElement(next);
        }
        // Record the frame's baseline.
        PushBaseline(next);
        mCurrentFrame = next;
        if (mCurrentFrame == mSubtree) {
          // If the current frame is mSubtree, we have now moved into it.
          mSubtreePosition = eWithinSubtree;
        }
      } else {
        for (;;) {
          // We want to move past the current frame.
          if (mCurrentFrame == mRootFrame) {
            // If we've reached the root frame, we're finished.
            mCurrentFrame = nullptr;
            break;
          }
          // Remove the current frame's position.
          mCurrentPosition -= mCurrentFrame->GetPosition();
          if (mCurrentFrame->GetContent()->IsSVGElement(nsGkAtoms::textPath)) {
            // Pop off the <textPath> frame if this is a <textPath>.
            mTextPathFrames.RemoveLastElement();
          }
          // Pop off the current baseline.
          PopBaseline();
          if (mCurrentFrame == mSubtree) {
            // If this was mSubtree, we have now moved past it.
            mSubtreePosition = eAfterSubtree;
          }
          next = mCurrentFrame->GetNextSibling();
          if (next) {
            // Moving to the next sibling.
            mCurrentPosition += next->GetPosition();
            if (next->GetContent()->IsSVGElement(nsGkAtoms::textPath)) {
              // Record this <textPath> frame.
              mTextPathFrames.AppendElement(next);
            }
            // Record the frame's baseline.
            PushBaseline(next);
            mCurrentFrame = next;
            if (mCurrentFrame == mSubtree) {
              // If the current frame is mSubtree, we have now moved into it.
              mSubtreePosition = eWithinSubtree;
            }
            break;
          }
          if (mCurrentFrame == mSubtree) {
            // If there is no next sibling frame, and the current frame is
            // mSubtree, we have now moved past it.
            mSubtreePosition = eAfterSubtree;
          }
          // Ascend out of this frame.
          mCurrentFrame = mCurrentFrame->GetParent();
        }
      }
    } while (mCurrentFrame && !IsNonEmptyTextFrame(mCurrentFrame));
  }

  return Current();
}

void TextFrameIterator::PushBaseline(nsIFrame* aNextFrame) {
  StyleDominantBaseline baseline = aNextFrame->StyleSVG()->mDominantBaseline;
  mBaselines.AppendElement(baseline);
}

void TextFrameIterator::PopBaseline() {
  NS_ASSERTION(!mBaselines.IsEmpty(), "popped too many baselines");
  mBaselines.RemoveLastElement();
}

// -----------------------------------------------------------------------------
// TextRenderedRunIterator

/**
 * Iterator for TextRenderedRun objects for the SVGTextFrame.
 */
class TextRenderedRunIterator {
 public:
  /**
   * Values for the aFilter argument of the constructor, to indicate which
   * frames we should be limited to iterating TextRenderedRun objects for.
   */
  enum RenderedRunFilter {
    // Iterate TextRenderedRuns for all nsTextFrames.
    eAllFrames,
    // Iterate only TextRenderedRuns for nsTextFrames that are
    // visibility:visible.
    eVisibleFrames
  };

  /**
   * Constructs a TextRenderedRunIterator with an optional frame subtree to
   * restrict iterated rendered runs to.
   *
   * @param aSVGTextFrame The SVGTextFrame whose rendered runs to iterate
   *   through.
   * @param aFilter Indicates whether to iterate rendered runs for non-visible
   *   nsTextFrames.
   * @param aSubtree An optional frame subtree to restrict iterated rendered
   *   runs to.
   */
  explicit TextRenderedRunIterator(SVGTextFrame* aSVGTextFrame,
                                   RenderedRunFilter aFilter = eAllFrames,
                                   const nsIFrame* aSubtree = nullptr)
      : mFrameIterator(FrameIfAnonymousChildReflowed(aSVGTextFrame), aSubtree),
        mFilter(aFilter),
        mTextElementCharIndex(0),
        mFrameStartTextElementCharIndex(0),
        mFontSizeScaleFactor(aSVGTextFrame->mFontSizeScaleFactor),
        mCurrent(First()) {}

  /**
   * Constructs a TextRenderedRunIterator with a content subtree to restrict
   * iterated rendered runs to.
   *
   * @param aSVGTextFrame The SVGTextFrame whose rendered runs to iterate
   *   through.
   * @param aFilter Indicates whether to iterate rendered runs for non-visible
   *   nsTextFrames.
   * @param aSubtree A content subtree to restrict iterated rendered runs to.
   */
  TextRenderedRunIterator(SVGTextFrame* aSVGTextFrame,
                          RenderedRunFilter aFilter, nsIContent* aSubtree)
      : mFrameIterator(FrameIfAnonymousChildReflowed(aSVGTextFrame), aSubtree),
        mFilter(aFilter),
        mTextElementCharIndex(0),
        mFrameStartTextElementCharIndex(0),
        mFontSizeScaleFactor(aSVGTextFrame->mFontSizeScaleFactor),
        mCurrent(First()) {}

  /**
   * Returns the current TextRenderedRun.
   */
  TextRenderedRun Current() const { return mCurrent; }

  /**
   * Advances to the next TextRenderedRun and returns it.
   */
  TextRenderedRun Next();

 private:
  /**
   * Returns the root SVGTextFrame this iterator is for.
   */
  SVGTextFrame* Root() const { return mFrameIterator.Root(); }

  /**
   * Advances to the first TextRenderedRun and returns it.
   */
  TextRenderedRun First();

  /**
   * The frame iterator to use.
   */
  TextFrameIterator mFrameIterator;

  /**
   * The filter indicating which TextRenderedRuns to return.
   */
  RenderedRunFilter mFilter;

  /**
   * The character index across the entire <text> element we are currently
   * up to.
   */
  uint32_t mTextElementCharIndex;

  /**
   * The character index across the entire <text> for the start of the current
   * frame.
   */
  uint32_t mFrameStartTextElementCharIndex;

  /**
   * The font-size scale factor we used when constructing the nsTextFrames.
   */
  double mFontSizeScaleFactor;

  /**
   * The current TextRenderedRun.
   */
  TextRenderedRun mCurrent;
};

TextRenderedRun TextRenderedRunIterator::Next() {
  if (!mFrameIterator.Current()) {
    // If there are no more frames, then there are no more rendered runs to
    // return.
    mCurrent = TextRenderedRun();
    return mCurrent;
  }

  // The values we will use to initialize the TextRenderedRun with.
  nsTextFrame* frame;
  gfxPoint pt;
  double rotate;
  nscoord baseline;
  uint32_t offset, length;
  uint32_t charIndex;

  // We loop, because we want to skip over rendered runs that either aren't
  // within our subtree of interest, because they don't match the filter,
  // or because they are hidden due to having fallen off the end of a
  // <textPath>.
  for (;;) {
    if (mFrameIterator.IsAfterSubtree()) {
      mCurrent = TextRenderedRun();
      return mCurrent;
    }

    frame = mFrameIterator.Current();

    charIndex = mTextElementCharIndex;

    // Find the end of the rendered run, by looking through the
    // SVGTextFrame's positions array until we find one that is recorded
    // as a run boundary.
    uint32_t runStart,
        runEnd;  // XXX Replace runStart with mTextElementCharIndex.
    runStart = mTextElementCharIndex;
    runEnd = runStart + 1;
    while (runEnd < Root()->mPositions.Length() &&
           !Root()->mPositions[runEnd].mRunBoundary) {
      runEnd++;
    }

    // Convert the global run start/end indexes into an offset/length into the
    // current frame's Text.
    offset =
        frame->GetContentOffset() + runStart - mFrameStartTextElementCharIndex;
    length = runEnd - runStart;

    // If the end of the frame's content comes before the run boundary we found
    // in SVGTextFrame's position array, we need to shorten the rendered run.
    uint32_t contentEnd = frame->GetContentEnd();
    if (offset + length > contentEnd) {
      length = contentEnd - offset;
    }

    NS_ASSERTION(offset >= uint32_t(frame->GetContentOffset()),
                 "invalid offset");
    NS_ASSERTION(offset + length <= contentEnd, "invalid offset or length");

    // Get the frame's baseline position.
    frame->EnsureTextRun(nsTextFrame::eInflated);
    baseline = GetBaselinePosition(
        frame, frame->GetTextRun(nsTextFrame::eInflated),
        mFrameIterator.DominantBaseline(), mFontSizeScaleFactor);

    // Trim the offset/length to remove any leading/trailing white space.
    uint32_t untrimmedOffset = offset;
    uint32_t untrimmedLength = length;
    nsTextFrame::TrimmedOffsets trimmedOffsets =
        frame->GetTrimmedOffsets(frame->TextFragment());
    TrimOffsets(offset, length, trimmedOffsets);
    charIndex += offset - untrimmedOffset;

    // Get the position and rotation of the character that begins this
    // rendered run.
    pt = Root()->mPositions[charIndex].mPosition;
    rotate = Root()->mPositions[charIndex].mAngle;

    // Determine if we should skip this rendered run.
    bool skip = !mFrameIterator.IsWithinSubtree() ||
                Root()->mPositions[mTextElementCharIndex].mHidden;
    if (mFilter == eVisibleFrames) {
      skip = skip || !frame->StyleVisibility()->IsVisible();
    }

    // Update our global character index to move past the characters
    // corresponding to this rendered run.
    mTextElementCharIndex += untrimmedLength;

    // If we have moved past the end of the current frame's content, we need to
    // advance to the next frame.
    if (offset + untrimmedLength >= contentEnd) {
      mFrameIterator.Next();
      mTextElementCharIndex += mFrameIterator.UndisplayedCharacters();
      mFrameStartTextElementCharIndex = mTextElementCharIndex;
    }

    if (!mFrameIterator.Current()) {
      if (skip) {
        // That was the last frame, and we skipped this rendered run.  So we
        // have no rendered run to return.
        mCurrent = TextRenderedRun();
        return mCurrent;
      }
      break;
    }

    if (length && !skip) {
      // Only return a rendered run if it didn't get collapsed away entirely
      // (due to it being all white space) and if we don't want to skip it.
      break;
    }
  }

  mCurrent = TextRenderedRun(frame, pt, Root()->mLengthAdjustScaleFactor,
                             rotate, mFontSizeScaleFactor, baseline, offset,
                             length, charIndex);
  return mCurrent;
}

TextRenderedRun TextRenderedRunIterator::First() {
  if (!mFrameIterator.Current()) {
    return TextRenderedRun();
  }

  if (Root()->mPositions.IsEmpty()) {
    mFrameIterator.Close();
    return TextRenderedRun();
  }

  // Get the character index for the start of this rendered run, by skipping
  // any undisplayed characters.
  mTextElementCharIndex = mFrameIterator.UndisplayedCharacters();
  mFrameStartTextElementCharIndex = mTextElementCharIndex;

  return Next();
}

// -----------------------------------------------------------------------------
// CharIterator

/**
 * Iterator for characters within an SVGTextFrame.
 */
class MOZ_STACK_CLASS CharIterator {
  using Range = gfxTextRun::Range;

 public:
  /**
   * Values for the aFilter argument of the constructor, to indicate which
   * characters we should be iterating over.
   */
  enum CharacterFilter {
    // Iterate over all original characters from the DOM that are within valid
    // text content elements.
    eOriginal,
    // Iterate only over characters that are not skipped characters.
    eUnskipped,
    // Iterate only over characters that are addressable by the positioning
    // attributes x="", y="", etc.  This includes all characters after
    // collapsing white space as required by the value of 'white-space'.
    eAddressable,
  };

  /**
   * Constructs a CharIterator.
   *
   * @param aSVGTextFrame The SVGTextFrame whose characters to iterate
   *   through.
   * @param aFilter Indicates which characters to iterate over.
   * @param aSubtree A content subtree to track whether the current character
   *   is within.
   */
  CharIterator(SVGTextFrame* aSVGTextFrame, CharacterFilter aFilter,
               nsIContent* aSubtree, bool aPostReflow = true);

  /**
   * Returns whether the iterator is finished.
   */
  bool AtEnd() const { return !mFrameIterator.Current(); }

  /**
   * Advances to the next matching character.  Returns true if there was a
   * character to advance to, and false otherwise.
   */
  bool Next();

  /**
   * Advances ahead aCount matching characters.  Returns true if there were
   * enough characters to advance past, and false otherwise.
   */
  bool Next(uint32_t aCount);

  /**
   * Advances ahead up to aCount matching characters.
   */
  void NextWithinSubtree(uint32_t aCount);

  /**
   * Advances to the character with the specified index.  The index is in the
   * space of original characters (i.e., all DOM characters under the <text>
   * that are within valid text content elements).
   */
  bool AdvanceToCharacter(uint32_t aTextElementCharIndex);

  /**
   * Advances to the first matching character after the current nsTextFrame.
   */
  bool AdvancePastCurrentFrame();

  /**
   * Advances to the first matching character after the frames within
   * the current <textPath>.
   */
  bool AdvancePastCurrentTextPathFrame();

  /**
   * Advances to the first matching character of the subtree.  Returns true
   * if we successfully advance to the subtree, or if we are already within
   * the subtree.  Returns false if we are past the subtree.
   */
  bool AdvanceToSubtree();

  /**
   * Returns the nsTextFrame for the current character.
   */
  nsTextFrame* TextFrame() const { return mFrameIterator.Current(); }

  /**
   * Returns whether the iterator is within the subtree.
   */
  bool IsWithinSubtree() const { return mFrameIterator.IsWithinSubtree(); }

  /**
   * Returns whether the iterator is past the subtree.
   */
  bool IsAfterSubtree() const { return mFrameIterator.IsAfterSubtree(); }

  /**
   * Returns whether the current character is a skipped character.
   */
  bool IsOriginalCharSkipped() const {
    return mSkipCharsIterator.IsOriginalCharSkipped();
  }

  /**
   * Returns whether the current character is the start of a cluster and
   * ligature group.
   */
  bool IsClusterAndLigatureGroupStart() const {
    return mTextRun->IsLigatureGroupStart(
               mSkipCharsIterator.GetSkippedOffset()) &&
           mTextRun->IsClusterStart(mSkipCharsIterator.GetSkippedOffset());
  }

  /**
   * Returns the glyph run for the current character.
   */
  const gfxTextRun::GlyphRun& GlyphRun() const {
    return *mTextRun->FindFirstGlyphRunContaining(
        mSkipCharsIterator.GetSkippedOffset());
  }

  /**
   * Returns whether the current character is trimmed away when painting,
   * due to it being leading/trailing white space.
   */
  bool IsOriginalCharTrimmed() const;

  /**
   * Returns whether the current character is unaddressable from the SVG glyph
   * positioning attributes.
   */
  bool IsOriginalCharUnaddressable() const {
    return IsOriginalCharSkipped() || IsOriginalCharTrimmed();
  }

  /**
   * Returns the text run for the current character.
   */
  gfxTextRun* TextRun() const { return mTextRun; }

  /**
   * Returns the current character index.
   */
  uint32_t TextElementCharIndex() const { return mTextElementCharIndex; }

  /**
   * Returns the character index for the start of the cluster/ligature group it
   * is part of.
   */
  uint32_t GlyphStartTextElementCharIndex() const {
    return mGlyphStartTextElementCharIndex;
  }

  /**
   * Gets the advance, in user units, of the current character.  If the
   * character is a part of ligature, then the advance returned will be
   * a fraction of the ligature glyph's advance.
   *
   * @param aContext The context to use for unit conversions.
   */
  gfxFloat GetAdvance(nsPresContext* aContext) const;

  /**
   * Returns the frame corresponding to the <textPath> that the current
   * character is within.
   */
  nsIFrame* TextPathFrame() const { return mFrameIterator.TextPathFrame(); }

#ifdef DEBUG
  /**
   * Returns the subtree we were constructed with.
   */
  nsIContent* GetSubtree() const { return mSubtree; }

  /**
   * Returns the CharacterFilter mode in use.
   */
  CharacterFilter Filter() const { return mFilter; }
#endif

 private:
  /**
   * Advances to the next character without checking it against the filter.
   * Returns true if there was a next character to advance to, or false
   * otherwise.
   */
  bool NextCharacter();

  /**
   * Returns whether the current character matches the filter.
   */
  bool MatchesFilter() const;

  /**
   * If this is the start of a glyph, record it.
   */
  void UpdateGlyphStartTextElementCharIndex() {
    if (!IsOriginalCharSkipped() && IsClusterAndLigatureGroupStart()) {
      mGlyphStartTextElementCharIndex = mTextElementCharIndex;
    }
  }

  /**
   * The filter to use.
   */
  CharacterFilter mFilter;

  /**
   * The iterator for text frames.
   */
  TextFrameIterator mFrameIterator;

#ifdef DEBUG
  /**
   * The subtree we were constructed with.
   */
  nsIContent* const mSubtree;
#endif

  /**
   * A gfxSkipCharsIterator for the text frame the current character is
   * a part of.
   */
  gfxSkipCharsIterator mSkipCharsIterator;

  // Cache for information computed by IsOriginalCharTrimmed.
  mutable nsTextFrame* mFrameForTrimCheck;
  mutable uint32_t mTrimmedOffset;
  mutable uint32_t mTrimmedLength;

  /**
   * The text run the current character is a part of.
   */
  gfxTextRun* mTextRun;

  /**
   * The current character's index.
   */
  uint32_t mTextElementCharIndex;

  /**
   * The index of the character that starts the cluster/ligature group the
   * current character is a part of.
   */
  uint32_t mGlyphStartTextElementCharIndex;

  /**
   * The scale factor to apply to glyph advances returned by
   * GetAdvance etc. to take into account textLength="".
   */
  float mLengthAdjustScaleFactor;

  /**
   * Whether the instance of this class is being used after reflow has occurred
   * or not.
   */
  bool mPostReflow;
};

CharIterator::CharIterator(SVGTextFrame* aSVGTextFrame,
                           CharIterator::CharacterFilter aFilter,
                           nsIContent* aSubtree, bool aPostReflow)
    : mFilter(aFilter),
      mFrameIterator(aSVGTextFrame, aSubtree),
#ifdef DEBUG
      mSubtree(aSubtree),
#endif
      mFrameForTrimCheck(nullptr),
      mTrimmedOffset(0),
      mTrimmedLength(0),
      mTextRun(nullptr),
      mTextElementCharIndex(0),
      mGlyphStartTextElementCharIndex(0),
      mLengthAdjustScaleFactor(aSVGTextFrame->mLengthAdjustScaleFactor),
      mPostReflow(aPostReflow) {
  if (!AtEnd()) {
    mSkipCharsIterator = TextFrame()->EnsureTextRun(nsTextFrame::eInflated);
    mTextRun = TextFrame()->GetTextRun(nsTextFrame::eInflated);
    mTextElementCharIndex = mFrameIterator.UndisplayedCharacters();
    UpdateGlyphStartTextElementCharIndex();
    if (!MatchesFilter()) {
      Next();
    }
  }
}

bool CharIterator::Next() {
  while (NextCharacter()) {
    if (MatchesFilter()) {
      return true;
    }
  }
  return false;
}

bool CharIterator::Next(uint32_t aCount) {
  if (aCount == 0 && AtEnd()) {
    return false;
  }
  while (aCount) {
    if (!Next()) {
      return false;
    }
    aCount--;
  }
  return true;
}

void CharIterator::NextWithinSubtree(uint32_t aCount) {
  while (IsWithinSubtree() && aCount) {
    --aCount;
    if (!Next()) {
      return;
    }
  }
}

bool CharIterator::AdvanceToCharacter(uint32_t aTextElementCharIndex) {
  while (mTextElementCharIndex < aTextElementCharIndex) {
    if (!Next()) {
      return false;
    }
  }
  return true;
}

bool CharIterator::AdvancePastCurrentFrame() {
  // XXX Can do this better than one character at a time if it matters.
  nsTextFrame* currentFrame = TextFrame();
  do {
    if (!Next()) {
      return false;
    }
  } while (TextFrame() == currentFrame);
  return true;
}

bool CharIterator::AdvancePastCurrentTextPathFrame() {
  nsIFrame* currentTextPathFrame = TextPathFrame();
  NS_ASSERTION(currentTextPathFrame,
               "expected AdvancePastCurrentTextPathFrame to be called only "
               "within a text path frame");
  do {
    if (!AdvancePastCurrentFrame()) {
      return false;
    }
  } while (TextPathFrame() == currentTextPathFrame);
  return true;
}

bool CharIterator::AdvanceToSubtree() {
  while (!IsWithinSubtree()) {
    if (IsAfterSubtree()) {
      return false;
    }
    if (!AdvancePastCurrentFrame()) {
      return false;
    }
  }
  return true;
}

bool CharIterator::IsOriginalCharTrimmed() const {
  if (mFrameForTrimCheck != TextFrame()) {
    // Since we do a lot of trim checking, we cache the trimmed offsets and
    // lengths while we are in the same frame.
    mFrameForTrimCheck = TextFrame();
    uint32_t offset = mFrameForTrimCheck->GetContentOffset();
    uint32_t length = mFrameForTrimCheck->GetContentLength();
    nsTextFrame::TrimmedOffsets trim = mFrameForTrimCheck->GetTrimmedOffsets(
        mFrameForTrimCheck->TextFragment(),
        (mPostReflow ? nsTextFrame::TrimmedOffsetFlags::Default
                     : nsTextFrame::TrimmedOffsetFlags::NotPostReflow));
    TrimOffsets(offset, length, trim);
    mTrimmedOffset = offset;
    mTrimmedLength = length;
  }

  // A character is trimmed if it is outside the mTrimmedOffset/mTrimmedLength
  // range and it is not a significant newline character.
  uint32_t index = mSkipCharsIterator.GetOriginalOffset();
  return !(
      (index >= mTrimmedOffset && index < mTrimmedOffset + mTrimmedLength) ||
      (index >= mTrimmedOffset + mTrimmedLength &&
       mFrameForTrimCheck->StyleText()->NewlineIsSignificant(
           mFrameForTrimCheck) &&
       mFrameForTrimCheck->TextFragment()->CharAt(index) == '\n'));
}

gfxFloat CharIterator::GetAdvance(nsPresContext* aContext) const {
  float cssPxPerDevPx =
      nsPresContext::AppUnitsToFloatCSSPixels(aContext->AppUnitsPerDevPixel());

  gfxSkipCharsIterator start =
      TextFrame()->EnsureTextRun(nsTextFrame::eInflated);
  nsTextFrame::PropertyProvider provider(TextFrame(), start);

  uint32_t offset = mSkipCharsIterator.GetSkippedOffset();
  gfxFloat advance =
      mTextRun->GetAdvanceWidth(Range(offset, offset + 1), &provider);
  return aContext->AppUnitsToGfxUnits(advance) * mLengthAdjustScaleFactor *
         cssPxPerDevPx;
}

bool CharIterator::NextCharacter() {
  if (AtEnd()) {
    return false;
  }

  mTextElementCharIndex++;

  // Advance within the current text run.
  mSkipCharsIterator.AdvanceOriginal(1);
  if (mSkipCharsIterator.GetOriginalOffset() < TextFrame()->GetContentEnd()) {
    // We're still within the part of the text run for the current text frame.
    UpdateGlyphStartTextElementCharIndex();
    return true;
  }

  // Advance to the next frame.
  mFrameIterator.Next();

  // Skip any undisplayed characters.
  uint32_t undisplayed = mFrameIterator.UndisplayedCharacters();
  mTextElementCharIndex += undisplayed;
  if (!TextFrame()) {
    // We're at the end.
    mSkipCharsIterator = gfxSkipCharsIterator();
    return false;
  }

  mSkipCharsIterator = TextFrame()->EnsureTextRun(nsTextFrame::eInflated);
  mTextRun = TextFrame()->GetTextRun(nsTextFrame::eInflated);
  UpdateGlyphStartTextElementCharIndex();
  return true;
}

bool CharIterator::MatchesFilter() const {
  switch (mFilter) {
    case eOriginal:
      return true;
    case eUnskipped:
      return !IsOriginalCharSkipped();
    case eAddressable:
      return !IsOriginalCharSkipped() && !IsOriginalCharUnaddressable();
  }
  MOZ_ASSERT_UNREACHABLE("Invalid mFilter value");
  return true;
}

// -----------------------------------------------------------------------------
// SVGTextDrawPathCallbacks

/**
 * Text frame draw callback class that paints the text and text decoration parts
 * of an nsTextFrame using SVG painting properties, and selection backgrounds
 * and decorations as they would normally.
 *
 * An instance of this class is passed to nsTextFrame::PaintText if painting
 * cannot be done directly (e.g. if we are using an SVG pattern fill, stroking
 * the text, etc.).
 */
class SVGTextDrawPathCallbacks final : public nsTextFrame::DrawPathCallbacks {
  using imgDrawingParams = image::imgDrawingParams;

 public:
  /**
   * Constructs an SVGTextDrawPathCallbacks.
   *
   * @param aSVGTextFrame The ancestor text frame.
   * @param aContext The context to use for painting.
   * @param aFrame The nsTextFrame to paint.
   * @param aCanvasTM The transformation matrix to set when painting; this
   *   should be the FOR_OUTERSVG_TM canvas TM of the text, so that
   *   paint servers are painted correctly.
   * @param aImgParams Whether we need to synchronously decode images.
   * @param aShouldPaintSVGGlyphs Whether SVG glyphs should be painted.
   */
  SVGTextDrawPathCallbacks(SVGTextFrame* aSVGTextFrame, gfxContext& aContext,
                           nsTextFrame* aFrame, const gfxMatrix& aCanvasTM,
                           imgDrawingParams& aImgParams,
                           bool aShouldPaintSVGGlyphs)
      : DrawPathCallbacks(aShouldPaintSVGGlyphs),
        mSVGTextFrame(aSVGTextFrame),
        mContext(aContext),
        mFrame(aFrame),
        mCanvasTM(aCanvasTM),
        mImgParams(aImgParams),
        mColor(0) {}

  void NotifySelectionBackgroundNeedsFill(const Rect& aBackgroundRect,
                                          nscolor aColor,
                                          DrawTarget& aDrawTarget) override;
  void PaintDecorationLine(Rect aPath, nscolor aColor) override;
  void PaintSelectionDecorationLine(Rect aPath, nscolor aColor) override;
  void NotifyBeforeText(nscolor aColor) override;
  void NotifyGlyphPathEmitted() override;
  void NotifyAfterText() override;

 private:
  void SetupContext();

  bool IsClipPathChild() const {
    return mSVGTextFrame->HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD);
  }

  /**
   * Paints a piece of text geometry.  This is called when glyphs
   * or text decorations have been emitted to the gfxContext.
   */
  void HandleTextGeometry();

  /**
   * Sets the gfxContext paint to the appropriate color or pattern
   * for filling text geometry.
   */
  void MakeFillPattern(GeneralPattern* aOutPattern);

  /**
   * Fills and strokes a piece of text geometry, using group opacity
   * if the selection style requires it.
   */
  void FillAndStrokeGeometry();

  /**
   * Fills a piece of text geometry.
   */
  void FillGeometry();

  /**
   * Strokes a piece of text geometry.
   */
  void StrokeGeometry();

  SVGTextFrame* const mSVGTextFrame;
  gfxContext& mContext;
  nsTextFrame* const mFrame;
  const gfxMatrix& mCanvasTM;
  imgDrawingParams& mImgParams;

  /**
   * The color that we were last told from one of the path callback functions.
   * This color can be the special NS_SAME_AS_FOREGROUND_COLOR,
   * NS_40PERCENT_FOREGROUND_COLOR and NS_TRANSPARENT colors when we are
   * painting selections or IME decorations.
   */
  nscolor mColor;
};

void SVGTextDrawPathCallbacks::NotifySelectionBackgroundNeedsFill(
    const Rect& aBackgroundRect, nscolor aColor, DrawTarget& aDrawTarget) {
  if (IsClipPathChild()) {
    // Don't paint selection backgrounds when in a clip path.
    return;
  }

  mColor = aColor;  // currently needed by MakeFillPattern

  GeneralPattern fillPattern;
  MakeFillPattern(&fillPattern);
  if (fillPattern.GetPattern()) {
    DrawOptions drawOptions(aColor == NS_40PERCENT_FOREGROUND_COLOR ? 0.4
                                                                    : 1.0);
    aDrawTarget.FillRect(aBackgroundRect, fillPattern, drawOptions);
  }
}

void SVGTextDrawPathCallbacks::NotifyBeforeText(nscolor aColor) {
  mColor = aColor;
  SetupContext();
  mContext.NewPath();
}

void SVGTextDrawPathCallbacks::NotifyGlyphPathEmitted() {
  HandleTextGeometry();
  mContext.NewPath();
}

void SVGTextDrawPathCallbacks::NotifyAfterText() { mContext.Restore(); }

void SVGTextDrawPathCallbacks::PaintDecorationLine(Rect aPath, nscolor aColor) {
  mColor = aColor;
  AntialiasMode aaMode =
      SVGUtils::ToAntialiasMode(mFrame->StyleText()->mTextRendering);

  mContext.Save();
  mContext.NewPath();
  mContext.SetAntialiasMode(aaMode);
  mContext.Rectangle(ThebesRect(aPath));
  HandleTextGeometry();
  mContext.NewPath();
  mContext.Restore();
}

void SVGTextDrawPathCallbacks::PaintSelectionDecorationLine(Rect aPath,
                                                            nscolor aColor) {
  if (IsClipPathChild()) {
    // Don't paint selection decorations when in a clip path.
    return;
  }

  mColor = aColor;

  mContext.Save();
  mContext.NewPath();
  mContext.Rectangle(ThebesRect(aPath));
  FillAndStrokeGeometry();
  mContext.Restore();
}

void SVGTextDrawPathCallbacks::SetupContext() {
  mContext.Save();

  // XXX This is copied from nsSVGGlyphFrame::Render, but cairo doesn't actually
  // seem to do anything with the antialias mode.  So we can perhaps remove it,
  // or make SetAntialiasMode set cairo text antialiasing too.
  switch (mFrame->StyleText()->mTextRendering) {
    case StyleTextRendering::Optimizespeed:
      mContext.SetAntialiasMode(AntialiasMode::NONE);
      break;
    default:
      mContext.SetAntialiasMode(AntialiasMode::SUBPIXEL);
      break;
  }
}

void SVGTextDrawPathCallbacks::HandleTextGeometry() {
  if (IsClipPathChild()) {
    RefPtr<Path> path = mContext.GetPath();
    ColorPattern white(
        DeviceColor(1.f, 1.f, 1.f, 1.f));  // for masking, so no ToDeviceColor
    mContext.GetDrawTarget()->Fill(path, white);
  } else {
    // Normal painting.
    gfxContextMatrixAutoSaveRestore saveMatrix(&mContext);
    mContext.SetMatrixDouble(mCanvasTM);

    FillAndStrokeGeometry();
  }
}

void SVGTextDrawPathCallbacks::MakeFillPattern(GeneralPattern* aOutPattern) {
  if (mColor == NS_SAME_AS_FOREGROUND_COLOR ||
      mColor == NS_40PERCENT_FOREGROUND_COLOR) {
    SVGUtils::MakeFillPatternFor(mFrame, &mContext, aOutPattern, mImgParams);
    return;
  }

  if (mColor == NS_TRANSPARENT) {
    return;
  }

  aOutPattern->InitColorPattern(ToDeviceColor(mColor));
}

void SVGTextDrawPathCallbacks::FillAndStrokeGeometry() {
  gfxGroupForBlendAutoSaveRestore autoGroupForBlend(&mContext);
  if (mColor == NS_40PERCENT_FOREGROUND_COLOR) {
    autoGroupForBlend.PushGroupForBlendBack(gfxContentType::COLOR_ALPHA, 0.4f);
  }

  uint32_t paintOrder = mFrame->StyleSVG()->mPaintOrder;
  if (!paintOrder) {
    FillGeometry();
    StrokeGeometry();
  } else {
    while (paintOrder) {
      auto component = StylePaintOrder(paintOrder & kPaintOrderMask);
      switch (component) {
        case StylePaintOrder::Fill:
          FillGeometry();
          break;
        case StylePaintOrder::Stroke:
          StrokeGeometry();
          break;
        default:
          MOZ_FALLTHROUGH_ASSERT("Unknown paint-order value");
        case StylePaintOrder::Markers:
        case StylePaintOrder::Normal:
          break;
      }
      paintOrder >>= kPaintOrderShift;
    }
  }
}

void SVGTextDrawPathCallbacks::FillGeometry() {
  GeneralPattern fillPattern;
  MakeFillPattern(&fillPattern);
  if (fillPattern.GetPattern()) {
    RefPtr<Path> path = mContext.GetPath();
    FillRule fillRule = SVGUtils::ToFillRule(mFrame->StyleSVG()->mFillRule);
    if (fillRule != path->GetFillRule()) {
      RefPtr<PathBuilder> builder = path->CopyToBuilder(fillRule);
      path = builder->Finish();
    }
    mContext.GetDrawTarget()->Fill(path, fillPattern);
  }
}

void SVGTextDrawPathCallbacks::StrokeGeometry() {
  // We don't paint the stroke when we are filling with a selection color.
  if (mColor == NS_SAME_AS_FOREGROUND_COLOR ||
      mColor == NS_40PERCENT_FOREGROUND_COLOR) {
    if (SVGUtils::HasStroke(mFrame, /*aContextPaint*/ nullptr)) {
      GeneralPattern strokePattern;
      SVGUtils::MakeStrokePatternFor(mFrame, &mContext, &strokePattern,
                                     mImgParams, /*aContextPaint*/ nullptr);
      if (strokePattern.GetPattern()) {
        if (!mFrame->GetParent()->GetContent()->IsSVGElement()) {
          // The cast that follows would be unsafe
          MOZ_ASSERT(false, "Our nsTextFrame's parent's content should be SVG");
          return;
        }
        SVGElement* svgOwner =
            static_cast<SVGElement*>(mFrame->GetParent()->GetContent());

        // Apply any stroke-specific transform
        gfxMatrix outerSVGToUser;
        if (SVGUtils::GetNonScalingStrokeTransform(mFrame, &outerSVGToUser) &&
            outerSVGToUser.Invert()) {
          mContext.Multiply(outerSVGToUser);
        }

        RefPtr<Path> path = mContext.GetPath();
        SVGContentUtils::AutoStrokeOptions strokeOptions;
        SVGContentUtils::GetStrokeOptions(&strokeOptions, svgOwner,
                                          mFrame->Style(),
                                          /*aContextPaint*/ nullptr);
        DrawOptions drawOptions;
        drawOptions.mAntialiasMode =
            SVGUtils::ToAntialiasMode(mFrame->StyleText()->mTextRendering);
        mContext.GetDrawTarget()->Stroke(path, strokePattern, strokeOptions);
      }
    }
  }
}

// ============================================================================
// SVGTextFrame

// ----------------------------------------------------------------------------
// Display list item

class DisplaySVGText final : public DisplaySVGItem {
 public:
  DisplaySVGText(nsDisplayListBuilder* aBuilder, SVGTextFrame* aFrame)
      : DisplaySVGItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(DisplaySVGText);
  }

  MOZ_COUNTED_DTOR_OVERRIDE(DisplaySVGText)

  NS_DISPLAY_DECL_NAME("DisplaySVGText", TYPE_SVG_TEXT)

  nsDisplayItemGeometry* AllocateGeometry(
      nsDisplayListBuilder* aBuilder) override {
    return new nsDisplayItemGenericGeometry(this, aBuilder);
  }

  nsRect GetComponentAlphaBounds(
      nsDisplayListBuilder* aBuilder) const override {
    bool snap;
    return GetBounds(aBuilder, &snap);
  }
};

// ---------------------------------------------------------------------
// nsQueryFrame methods

NS_QUERYFRAME_HEAD(SVGTextFrame)
  NS_QUERYFRAME_ENTRY(SVGTextFrame)
NS_QUERYFRAME_TAIL_INHERITING(SVGDisplayContainerFrame)

}  // namespace mozilla

// ---------------------------------------------------------------------
// Implementation

nsIFrame* NS_NewSVGTextFrame(mozilla::PresShell* aPresShell,
                             mozilla::ComputedStyle* aStyle) {
  return new (aPresShell)
      mozilla::SVGTextFrame(aStyle, aPresShell->GetPresContext());
}

namespace mozilla {

NS_IMPL_FRAMEARENA_HELPERS(SVGTextFrame)

// ---------------------------------------------------------------------
// nsIFrame methods

void SVGTextFrame::Init(nsIContent* aContent, nsContainerFrame* aParent,
                        nsIFrame* aPrevInFlow) {
  NS_ASSERTION(aContent->IsSVGElement(nsGkAtoms::text),
               "Content is not an SVG text");

  SVGDisplayContainerFrame::Init(aContent, aParent, aPrevInFlow);
  AddStateBits(aParent->GetStateBits() & NS_STATE_SVG_CLIPPATH_CHILD);

  mMutationObserver = new MutationObserver(this);

  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    // We're inserting a new <text> element into a non-display context.
    // Ensure that we get reflowed.
    ScheduleReflowSVGNonDisplayText(
        IntrinsicDirty::FrameAncestorsAndDescendants);
  }
}

void SVGTextFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                    const nsDisplayListSet& aLists) {
  if (IsSubtreeDirty()) {
    // We can sometimes be asked to paint before reflow happens and we
    // have updated mPositions, etc.  In this case, we just avoid
    // painting.
    return;
  }
  if (!IsVisibleForPainting() && aBuilder->IsForPainting()) {
    return;
  }
  DisplayOutline(aBuilder, aLists);
  aLists.Content()->AppendNewToTop<DisplaySVGText>(aBuilder, this);
}

nsresult SVGTextFrame::AttributeChanged(int32_t aNameSpaceID,
                                        nsAtom* aAttribute, int32_t aModType) {
  if (aNameSpaceID != kNameSpaceID_None) {
    return NS_OK;
  }

  if (aAttribute == nsGkAtoms::transform) {
    // We don't invalidate for transform changes (the layers code does that).
    // Also note that SVGTransformableElement::GetAttributeChangeHint will
    // return nsChangeHint_UpdateOverflow for "transform" attribute changes
    // and cause DoApplyRenderingChangeToTree to make the SchedulePaint call.

    if (!HasAnyStateBits(NS_FRAME_FIRST_REFLOW) && mCanvasTM &&
        mCanvasTM->IsSingular()) {
      // We won't have calculated the glyph positions correctly.
      NotifyGlyphMetricsChange(false);
    }
    mCanvasTM = nullptr;
  } else if (IsGlyphPositioningAttribute(aAttribute) ||
             aAttribute == nsGkAtoms::textLength ||
             aAttribute == nsGkAtoms::lengthAdjust) {
    NotifyGlyphMetricsChange(false);
  }

  return NS_OK;
}

void SVGTextFrame::ReflowSVGNonDisplayText() {
  MOZ_ASSERT(SVGUtils::AnyOuterSVGIsCallingReflowSVG(this),
             "only call ReflowSVGNonDisplayText when an outer SVG frame is "
             "under ReflowSVG");
  MOZ_ASSERT(HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "only call ReflowSVGNonDisplayText if the frame is "
             "NS_FRAME_IS_NONDISPLAY");

  // We had a style change, so we mark this frame as dirty so that the next
  // time it is painted, we reflow the anonymous block frame.
  MarkSubtreeDirty();

  // Finally, we need to actually reflow the anonymous block frame and update
  // mPositions, in case we are being reflowed immediately after a DOM
  // mutation that needs frame reconstruction.
  MaybeReflowAnonymousBlockChild();
  UpdateGlyphPositioning();
}

void SVGTextFrame::ScheduleReflowSVGNonDisplayText(IntrinsicDirty aReason) {
  MOZ_ASSERT(!SVGUtils::OuterSVGIsCallingReflowSVG(this),
             "do not call ScheduleReflowSVGNonDisplayText when the outer SVG "
             "frame is under ReflowSVG");
  MOZ_ASSERT(!HasAnyStateBits(NS_STATE_SVG_TEXT_IN_REFLOW),
             "do not call ScheduleReflowSVGNonDisplayText while reflowing the "
             "anonymous block child");

  // We need to find an ancestor frame that we can call FrameNeedsReflow
  // on that will cause the document to be marked as needing relayout,
  // and for that ancestor (or some further ancestor) to be marked as
  // a root to reflow.  We choose the closest ancestor frame that is not
  // NS_FRAME_IS_NONDISPLAY and which is either an outer SVG frame or a
  // non-SVG frame.  (We don't consider displayed SVG frame ancestors other
  // than SVGOuterSVGFrame, since calling FrameNeedsReflow on those other
  // SVG frames would do a bunch of unnecessary work on the SVG frames up to
  // the SVGOuterSVGFrame.)

  nsIFrame* f = this;
  while (f) {
    if (!f->HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
      if (f->IsSubtreeDirty()) {
        // This is a displayed frame, so if it is already dirty, we will be
        // reflowed soon anyway.  No need to call FrameNeedsReflow again, then.
        return;
      }
      if (!f->HasAnyStateBits(NS_FRAME_SVG_LAYOUT)) {
        break;
      }
      f->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
    }
    f = f->GetParent();
  }

  MOZ_ASSERT(f, "should have found an ancestor frame to reflow");

  PresShell()->FrameNeedsReflow(f, aReason, NS_FRAME_IS_DIRTY);
}

NS_IMPL_ISUPPORTS(SVGTextFrame::MutationObserver, nsIMutationObserver)

void SVGTextFrame::MutationObserver::ContentAppended(
    nsIContent* aFirstNewContent) {
  mFrame->NotifyGlyphMetricsChange(true);
}

void SVGTextFrame::MutationObserver::ContentInserted(nsIContent* aChild) {
  mFrame->NotifyGlyphMetricsChange(true);
}

void SVGTextFrame::MutationObserver::ContentRemoved(
    nsIContent* aChild, nsIContent* aPreviousSibling) {
  mFrame->NotifyGlyphMetricsChange(true);
}

void SVGTextFrame::MutationObserver::CharacterDataChanged(
    nsIContent* aContent, const CharacterDataChangeInfo&) {
  mFrame->NotifyGlyphMetricsChange(true);
}

void SVGTextFrame::MutationObserver::AttributeChanged(
    Element* aElement, int32_t aNameSpaceID, nsAtom* aAttribute,
    int32_t aModType, const nsAttrValue* aOldValue) {
  if (!aElement->IsSVGElement()) {
    return;
  }

  // Attribute changes on this element will be handled by
  // SVGTextFrame::AttributeChanged.
  if (aElement == mFrame->GetContent()) {
    return;
  }

  mFrame->HandleAttributeChangeInDescendant(aElement, aNameSpaceID, aAttribute);
}

void SVGTextFrame::HandleAttributeChangeInDescendant(Element* aElement,
                                                     int32_t aNameSpaceID,
                                                     nsAtom* aAttribute) {
  if (aElement->IsSVGElement(nsGkAtoms::textPath)) {
    if (aNameSpaceID == kNameSpaceID_None &&
        (aAttribute == nsGkAtoms::startOffset ||
         aAttribute == nsGkAtoms::path || aAttribute == nsGkAtoms::side_)) {
      NotifyGlyphMetricsChange(false);
    } else if ((aNameSpaceID == kNameSpaceID_XLink ||
                aNameSpaceID == kNameSpaceID_None) &&
               aAttribute == nsGkAtoms::href) {
      // Blow away our reference, if any
      nsIFrame* childElementFrame = aElement->GetPrimaryFrame();
      if (childElementFrame) {
        SVGObserverUtils::RemoveTextPathObserver(childElementFrame);
        NotifyGlyphMetricsChange(false);
      }
    }
  } else {
    if (aNameSpaceID == kNameSpaceID_None &&
        IsGlyphPositioningAttribute(aAttribute)) {
      NotifyGlyphMetricsChange(false);
    }
  }
}

void SVGTextFrame::FindCloserFrameForSelection(
    const nsPoint& aPoint, FrameWithDistance* aCurrentBestFrame) {
  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    return;
  }

  UpdateGlyphPositioning();

  nsPresContext* presContext = PresContext();

  // Find the frame that has the closest rendered run rect to aPoint.
  TextRenderedRunIterator it(this);
  for (TextRenderedRun run = it.Current(); run.mFrame; run = it.Next()) {
    uint32_t flags = TextRenderedRun::eIncludeFill |
                     TextRenderedRun::eIncludeStroke |
                     TextRenderedRun::eNoHorizontalOverflow;
    SVGBBox userRect = run.GetUserSpaceRect(presContext, flags);
    float devPxPerCSSPx = presContext->CSSPixelsToDevPixels(1.f);
    userRect.Scale(devPxPerCSSPx);

    if (!userRect.IsEmpty()) {
      gfxMatrix m;
      nsRect rect =
          SVGUtils::ToCanvasBounds(userRect.ToThebesRect(), m, presContext);

      if (nsLayoutUtils::PointIsCloserToRect(aPoint, rect,
                                             aCurrentBestFrame->mXDistance,
                                             aCurrentBestFrame->mYDistance)) {
        aCurrentBestFrame->mFrame = run.mFrame;
      }
    }
  }
}

//----------------------------------------------------------------------
// ISVGDisplayableFrame methods

void SVGTextFrame::NotifySVGChanged(uint32_t aFlags) {
  MOZ_ASSERT(aFlags & (TRANSFORM_CHANGED | COORD_CONTEXT_CHANGED),
             "Invalidation logic may need adjusting");

  bool needNewBounds = false;
  bool needGlyphMetricsUpdate = false;
  bool needNewCanvasTM = false;

  if ((aFlags & COORD_CONTEXT_CHANGED) &&
      HasAnyStateBits(NS_STATE_SVG_POSITIONING_MAY_USE_PERCENTAGES)) {
    needGlyphMetricsUpdate = true;
  }

  if (aFlags & TRANSFORM_CHANGED) {
    needNewCanvasTM = true;
    if (mCanvasTM && mCanvasTM->IsSingular()) {
      // We won't have calculated the glyph positions correctly.
      needNewBounds = true;
      needGlyphMetricsUpdate = true;
    }
    if (StyleSVGReset()->HasNonScalingStroke()) {
      // Stroke currently contributes to our mRect, and our stroke depends on
      // the transform to our outer-<svg> if |vector-effect:non-scaling-stroke|.
      needNewBounds = true;
    }
  }

  // If the scale at which we computed our mFontSizeScaleFactor has changed by
  // at least a factor of two, reflow the text.  This avoids reflowing text
  // at every tick of a transform animation, but ensures our glyph metrics
  // do not get too far out of sync with the final font size on the screen.
  if (needNewCanvasTM && mLastContextScale != 0.0f) {
    mCanvasTM = nullptr;
    // If we are a non-display frame, then we don't want to call
    // GetCanvasTM(), since the context scale does not use it.
    gfxMatrix newTM =
        HasAnyStateBits(NS_FRAME_IS_NONDISPLAY) ? gfxMatrix() : GetCanvasTM();
    // Compare the old and new context scales.
    float scale = GetContextScale(newTM);
    float change = scale / mLastContextScale;
    if (change >= 2.0f || change <= 0.5f) {
      needNewBounds = true;
      needGlyphMetricsUpdate = true;
    }
  }

  if (needNewBounds) {
    // Ancestor changes can't affect how we render from the perspective of
    // any rendering observers that we may have, so we don't need to
    // invalidate them. We also don't need to invalidate ourself, since our
    // changed ancestor will have invalidated its entire area, which includes
    // our area.
    ScheduleReflowSVG();
  }

  if (needGlyphMetricsUpdate) {
    // If we are positioned using percentage values we need to update our
    // position whenever our viewport's dimensions change.  But only do this if
    // we have been reflowed once, otherwise the glyph positioning will be
    // wrong.  (We need to wait until bidi reordering has been done.)
    if (!HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
      NotifyGlyphMetricsChange(false);
    }
  }
}

/**
 * Gets the offset into a DOM node that the specified caret is positioned at.
 */
static int32_t GetCaretOffset(nsCaret* aCaret) {
  RefPtr<Selection> selection = aCaret->GetSelection();
  if (!selection) {
    return -1;
  }

  return selection->AnchorOffset();
}

/**
 * Returns whether the caret should be painted for a given TextRenderedRun
 * by checking whether the caret is in the range covered by the rendered run.
 *
 * @param aThisRun The TextRenderedRun to be painted.
 * @param aCaret The caret.
 */
static bool ShouldPaintCaret(const TextRenderedRun& aThisRun, nsCaret* aCaret) {
  int32_t caretOffset = GetCaretOffset(aCaret);

  if (caretOffset < 0) {
    return false;
  }

  return uint32_t(caretOffset) >= aThisRun.mTextFrameContentOffset &&
         uint32_t(caretOffset) < aThisRun.mTextFrameContentOffset +
                                     aThisRun.mTextFrameContentLength;
}

void SVGTextFrame::PaintSVG(gfxContext& aContext, const gfxMatrix& aTransform,
                            imgDrawingParams& aImgParams) {
  DrawTarget& aDrawTarget = *aContext.GetDrawTarget();
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (!kid) {
    return;
  }

  nsPresContext* presContext = PresContext();

  gfxMatrix initialMatrix = aContext.CurrentMatrixDouble();

  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    // If we are in a canvas DrawWindow call that used the
    // DRAWWINDOW_DO_NOT_FLUSH flag, then we may still have out
    // of date frames.  Just don't paint anything if they are
    // dirty.
    if (presContext->PresShell()->InDrawWindowNotFlushing() &&
        IsSubtreeDirty()) {
      return;
    }
    // Text frames inside <clipPath>, <mask>, etc. will never have had
    // ReflowSVG called on them, so call UpdateGlyphPositioning to do this now.
    UpdateGlyphPositioning();
  } else if (IsSubtreeDirty()) {
    // If we are asked to paint before reflow has recomputed mPositions etc.
    // directly via PaintSVG, rather than via a display list, then we need
    // to bail out here too.
    return;
  }

  const float epsilon = 0.0001;
  if (abs(mLengthAdjustScaleFactor) < epsilon) {
    // A zero scale factor can be caused by having forced the text length to
    // zero. In this situation there is nothing to show.
    return;
  }

  if (aTransform.IsSingular()) {
    NS_WARNING("Can't render text element!");
    return;
  }

  gfxMatrix matrixForPaintServers = aTransform * initialMatrix;

  // SVG frames' PaintSVG methods paint in CSS px, but normally frames paint in
  // dev pixels. Here we multiply a CSS-px-to-dev-pixel factor onto aTransform
  // so our non-SVG nsTextFrame children paint correctly.
  auto auPerDevPx = presContext->AppUnitsPerDevPixel();
  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(auPerDevPx);
  gfxMatrix canvasTMForChildren = aTransform;
  canvasTMForChildren.PreScale(cssPxPerDevPx, cssPxPerDevPx);
  initialMatrix.PreScale(1 / cssPxPerDevPx, 1 / cssPxPerDevPx);

  gfxContextMatrixAutoSaveRestore matSR(&aContext);
  aContext.NewPath();
  aContext.Multiply(canvasTMForChildren);
  gfxMatrix currentMatrix = aContext.CurrentMatrixDouble();

  RefPtr<nsCaret> caret = presContext->PresShell()->GetCaret();
  nsRect caretRect;
  nsIFrame* caretFrame = caret->GetPaintGeometry(&caretRect);

  gfxContextAutoSaveRestore ctxSR;
  TextRenderedRunIterator it(this, TextRenderedRunIterator::eVisibleFrames);
  TextRenderedRun run = it.Current();

  SVGContextPaint* outerContextPaint =
      SVGContextPaint::GetContextPaint(GetContent());

  while (run.mFrame) {
    nsTextFrame* frame = run.mFrame;

    RefPtr<SVGContextPaintImpl> contextPaint = new SVGContextPaintImpl();
    DrawMode drawMode = contextPaint->Init(&aDrawTarget, initialMatrix, frame,
                                           outerContextPaint, aImgParams);
    if (drawMode & DrawMode::GLYPH_STROKE) {
      ctxSR.EnsureSaved(&aContext);
      // This may change the gfxContext's transform (for non-scaling stroke),
      // in which case this needs to happen before we call SetMatrix() below.
      SVGUtils::SetupStrokeGeometry(frame->GetParent(), &aContext,
                                    outerContextPaint);
    }

    nscoord startEdge, endEdge;
    run.GetClipEdges(startEdge, endEdge);

    // Set up the transform for painting the text frame for the substring
    // indicated by the run.
    gfxMatrix runTransform = run.GetTransformFromUserSpaceForPainting(
                                 presContext, startEdge, endEdge) *
                             currentMatrix;
    aContext.SetMatrixDouble(runTransform);

    if (drawMode != DrawMode(0)) {
      bool paintSVGGlyphs;
      nsTextFrame::PaintTextParams params(&aContext);
      params.framePt = Point();
      params.dirtyRect =
          LayoutDevicePixel::FromAppUnits(frame->InkOverflowRect(), auPerDevPx);
      params.contextPaint = contextPaint;
      bool isSelected;
      if (HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD)) {
        params.state = nsTextFrame::PaintTextParams::GenerateTextMask;
        isSelected = false;
      } else {
        isSelected = frame->IsSelected();
      }
      gfxGroupForBlendAutoSaveRestore autoGroupForBlend(&aContext);
      float opacity = 1.0f;
      nsIFrame* ancestor = frame->GetParent();
      while (ancestor != this) {
        opacity *= ancestor->StyleEffects()->mOpacity;
        ancestor = ancestor->GetParent();
      }
      if (opacity < 1.0f) {
        autoGroupForBlend.PushGroupForBlendBack(gfxContentType::COLOR_ALPHA,
                                                opacity);
      }

      if (ShouldRenderAsPath(frame, paintSVGGlyphs)) {
        SVGTextDrawPathCallbacks callbacks(this, aContext, frame,
                                           matrixForPaintServers, aImgParams,
                                           paintSVGGlyphs);
        params.callbacks = &callbacks;
        frame->PaintText(params, startEdge, endEdge, nsPoint(), isSelected);
      } else {
        frame->PaintText(params, startEdge, endEdge, nsPoint(), isSelected);
      }
    }

    if (frame == caretFrame && ShouldPaintCaret(run, caret)) {
      // XXX Should we be looking at the fill/stroke colours to paint the
      // caret with, rather than using the color property?
      caret->PaintCaret(aDrawTarget, frame, nsPoint());
      aContext.NewPath();
    }

    run = it.Next();
  }
}

nsIFrame* SVGTextFrame::GetFrameForPoint(const gfxPoint& aPoint) {
  NS_ASSERTION(PrincipalChildList().FirstChild(), "must have a child frame");

  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    // Text frames inside <clipPath> will never have had ReflowSVG called on
    // them, so call UpdateGlyphPositioning to do this now.  (Text frames
    // inside <mask> and other non-display containers will never need to
    // be hit tested.)
    UpdateGlyphPositioning();
  } else {
    NS_ASSERTION(!IsSubtreeDirty(), "reflow should have happened");
  }

  // Hit-testing any clip-path will typically be a lot quicker than the
  // hit-testing of our text frames in the loop below, so we do the former up
  // front to avoid unnecessarily wasting cycles on the latter.
  if (!SVGUtils::HitTestClip(this, aPoint)) {
    return nullptr;
  }

  nsPresContext* presContext = PresContext();

  // Ideally we'd iterate backwards so that we can just return the first frame
  // that is under aPoint.  In practice this will rarely matter though since it
  // is rare for text in/under an SVG <text> element to overlap (i.e. the first
  // text frame that is hit will likely be the only text frame that is hit).

  TextRenderedRunIterator it(this);
  nsIFrame* hit = nullptr;
  for (TextRenderedRun run = it.Current(); run.mFrame; run = it.Next()) {
    uint16_t hitTestFlags = SVGUtils::GetGeometryHitTestFlags(run.mFrame);
    if (!hitTestFlags) {
      continue;
    }

    gfxMatrix m = run.GetTransformFromRunUserSpaceToUserSpace(presContext);
    if (!m.Invert()) {
      return nullptr;
    }

    gfxPoint pointInRunUserSpace = m.TransformPoint(aPoint);
    gfxRect frameRect = run.GetRunUserSpaceRect(
                               presContext, TextRenderedRun::eIncludeFill |
                                                TextRenderedRun::eIncludeStroke)
                            .ToThebesRect();

    if (Inside(frameRect, pointInRunUserSpace)) {
      hit = run.mFrame;
    }
  }
  return hit;
}

void SVGTextFrame::ReflowSVG() {
  MOZ_ASSERT(SVGUtils::AnyOuterSVGIsCallingReflowSVG(this),
             "This call is probaby a wasteful mistake");

  MOZ_ASSERT(!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY),
             "ReflowSVG mechanism not designed for this");

  if (!SVGUtils::NeedsReflowSVG(this)) {
    MOZ_ASSERT(!HasAnyStateBits(NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY |
                                NS_STATE_SVG_POSITIONING_DIRTY),
               "How did this happen?");
    return;
  }

  MaybeReflowAnonymousBlockChild();
  UpdateGlyphPositioning();

  nsPresContext* presContext = PresContext();

  SVGBBox r;
  TextRenderedRunIterator it(this, TextRenderedRunIterator::eAllFrames);
  for (TextRenderedRun run = it.Current(); run.mFrame; run = it.Next()) {
    uint32_t runFlags = 0;
    if (!run.mFrame->StyleSVG()->mFill.kind.IsNone()) {
      runFlags |= TextRenderedRun::eIncludeFill;
    }
    if (SVGUtils::HasStroke(run.mFrame)) {
      runFlags |= TextRenderedRun::eIncludeStroke;
    }
    // Our "visual" overflow rect needs to be valid for building display lists
    // for hit testing, which means that for certain values of 'pointer-events'
    // it needs to include the geometry of the fill or stroke even when the
    // fill/ stroke don't actually render (e.g. when stroke="none" or
    // stroke-opacity="0"). GetGeometryHitTestFlags accounts for
    // 'pointer-events'. The text-shadow is not part of the hit-test area.
    uint16_t hitTestFlags = SVGUtils::GetGeometryHitTestFlags(run.mFrame);
    if (hitTestFlags & SVG_HIT_TEST_FILL) {
      runFlags |= TextRenderedRun::eIncludeFill;
    }
    if (hitTestFlags & SVG_HIT_TEST_STROKE) {
      runFlags |= TextRenderedRun::eIncludeStroke;
    }

    if (runFlags) {
      r.UnionEdges(run.GetUserSpaceRect(presContext, runFlags));
    }
  }

  if (r.IsEmpty()) {
    mRect.SetEmpty();
  } else {
    mRect = nsLayoutUtils::RoundGfxRectToAppRect(r.ToThebesRect(),
                                                 AppUnitsPerCSSPixel());

    // Due to rounding issues when we have a transform applied, we sometimes
    // don't include an additional row of pixels.  For now, just inflate our
    // covered region.
    mRect.Inflate(ceil(presContext->AppUnitsPerDevPixel() / mLastContextScale));
  }

  if (HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // Make sure we have our filter property (if any) before calling
    // FinishAndStoreOverflow (subsequent filter changes are handled off
    // nsChangeHint_UpdateEffects):
    SVGObserverUtils::UpdateEffects(this);
  }

  // Now unset the various reflow bits. Do this before calling
  // FinishAndStoreOverflow since FinishAndStoreOverflow can require glyph
  // positions (to resolve transform-origin).
  RemoveStateBits(NS_FRAME_FIRST_REFLOW | NS_FRAME_IS_DIRTY |
                  NS_FRAME_HAS_DIRTY_CHILDREN);

  nsRect overflow = nsRect(nsPoint(0, 0), mRect.Size());
  OverflowAreas overflowAreas(overflow, overflow);
  FinishAndStoreOverflow(overflowAreas, mRect.Size());
}

/**
 * Converts SVGUtils::eBBox* flags into TextRenderedRun flags appropriate
 * for the specified rendered run.
 */
static uint32_t TextRenderedRunFlagsForBBoxContribution(
    const TextRenderedRun& aRun, uint32_t aBBoxFlags) {
  uint32_t flags = 0;
  if ((aBBoxFlags & SVGUtils::eBBoxIncludeFillGeometry) ||
      ((aBBoxFlags & SVGUtils::eBBoxIncludeFill) &&
       !aRun.mFrame->StyleSVG()->mFill.kind.IsNone())) {
    flags |= TextRenderedRun::eIncludeFill;
  }
  if ((aBBoxFlags & SVGUtils::eBBoxIncludeStrokeGeometry) ||
      ((aBBoxFlags & SVGUtils::eBBoxIncludeStroke) &&
       SVGUtils::HasStroke(aRun.mFrame))) {
    flags |= TextRenderedRun::eIncludeStroke;
  }
  return flags;
}

SVGBBox SVGTextFrame::GetBBoxContribution(const Matrix& aToBBoxUserspace,
                                          uint32_t aFlags) {
  NS_ASSERTION(PrincipalChildList().FirstChild(), "must have a child frame");
  SVGBBox bbox;

  if (aFlags & SVGUtils::eForGetClientRects) {
    Rect rect = NSRectToRect(mRect, AppUnitsPerCSSPixel());
    if (!rect.IsEmpty()) {
      bbox = aToBBoxUserspace.TransformBounds(rect);
    }
    return bbox;
  }

  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid && kid->IsSubtreeDirty()) {
    // Return an empty bbox if our kid's subtree is dirty. This may be called
    // in that situation, e.g. when we're building a display list after an
    // interrupted reflow. This can also be called during reflow before we've
    // been reflowed, e.g. if an earlier sibling is calling
    // FinishAndStoreOverflow and needs our parent's perspective matrix, which
    // depends on the SVG bbox contribution of this frame. In the latter
    // situation, when all siblings have been reflowed, the parent will compute
    // its perspective and rerun FinishAndStoreOverflow for all its children.
    return bbox;
  }

  UpdateGlyphPositioning();

  nsPresContext* presContext = PresContext();

  TextRenderedRunIterator it(this);
  for (TextRenderedRun run = it.Current(); run.mFrame; run = it.Next()) {
    uint32_t flags = TextRenderedRunFlagsForBBoxContribution(run, aFlags);
    gfxMatrix m = ThebesMatrix(aToBBoxUserspace);
    SVGBBox bboxForRun = run.GetUserSpaceRect(presContext, flags, &m);
    bbox.UnionEdges(bboxForRun);
  }

  return bbox;
}

//----------------------------------------------------------------------
// SVGTextFrame SVG DOM methods

/**
 * Returns whether the specified node has any non-empty Text
 * beneath it.
 */
static bool HasTextContent(nsIContent* aContent) {
  NS_ASSERTION(aContent, "expected non-null aContent");

  TextNodeIterator it(aContent);
  for (Text* text = it.Current(); text; text = it.Next()) {
    if (text->TextLength() != 0) {
      return true;
    }
  }
  return false;
}

/**
 * Returns the number of DOM characters beneath the specified node.
 */
static uint32_t GetTextContentLength(nsIContent* aContent) {
  NS_ASSERTION(aContent, "expected non-null aContent");

  uint32_t length = 0;
  TextNodeIterator it(aContent);
  for (Text* text = it.Current(); text; text = it.Next()) {
    length += text->TextLength();
  }
  return length;
}

int32_t SVGTextFrame::ConvertTextElementCharIndexToAddressableIndex(
    int32_t aIndex, nsIContent* aContent) {
  CharIterator it(this, CharIterator::eOriginal, aContent);
  if (!it.AdvanceToSubtree()) {
    return -1;
  }
  int32_t result = 0;
  int32_t textElementCharIndex;
  while (!it.AtEnd() && it.IsWithinSubtree()) {
    bool addressable = !it.IsOriginalCharUnaddressable();
    textElementCharIndex = it.TextElementCharIndex();
    it.Next();
    uint32_t delta = it.TextElementCharIndex() - textElementCharIndex;
    aIndex -= delta;
    if (addressable) {
      if (aIndex < 0) {
        return result;
      }
      result += delta;
    }
  }
  return -1;
}

/**
 * Implements the SVG DOM GetNumberOfChars method for the specified
 * text content element.
 */
uint32_t SVGTextFrame::GetNumberOfChars(nsIContent* aContent) {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    // We're never reflowed if we're under a non-SVG element that is
    // never reflowed (such as the HTML 'caption' element).
    return 0;
  }

  UpdateGlyphPositioning();

  uint32_t n = 0;
  CharIterator it(this, CharIterator::eAddressable, aContent);
  if (it.AdvanceToSubtree()) {
    while (!it.AtEnd() && it.IsWithinSubtree()) {
      n++;
      it.Next();
    }
  }
  return n;
}

/**
 * Implements the SVG DOM GetComputedTextLength method for the specified
 * text child element.
 */
float SVGTextFrame::GetComputedTextLength(nsIContent* aContent) {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    // We're never reflowed if we're under a non-SVG element that is
    // never reflowed (such as the HTML 'caption' element).
    //
    // If we ever decide that we need to return accurate values here,
    // we could do similar work to GetSubStringLength.
    return 0;
  }

  UpdateGlyphPositioning();

  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(
      PresContext()->AppUnitsPerDevPixel());

  nscoord length = 0;
  TextRenderedRunIterator it(this, TextRenderedRunIterator::eAllFrames,
                             aContent);
  for (TextRenderedRun run = it.Current(); run.mFrame; run = it.Next()) {
    length += run.GetAdvanceWidth();
  }

  return PresContext()->AppUnitsToGfxUnits(length) * cssPxPerDevPx *
         mLengthAdjustScaleFactor / mFontSizeScaleFactor;
}

/**
 * Implements the SVG DOM SelectSubString method for the specified
 * text content element.
 */
void SVGTextFrame::SelectSubString(nsIContent* aContent, uint32_t charnum,
                                   uint32_t nchars, ErrorResult& aRv) {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    // We're never reflowed if we're under a non-SVG element that is
    // never reflowed (such as the HTML 'caption' element).
    // XXXbz Should this just return without throwing like the no-frame case?
    aRv.ThrowInvalidStateError("No layout information available for SVG text");
    return;
  }

  UpdateGlyphPositioning();

  // Convert charnum/nchars from addressable characters relative to
  // aContent to global character indices.
  CharIterator chit(this, CharIterator::eAddressable, aContent);
  if (!chit.AdvanceToSubtree() || !chit.Next(charnum) ||
      chit.IsAfterSubtree()) {
    aRv.ThrowIndexSizeError("Character index out of range");
    return;
  }
  charnum = chit.TextElementCharIndex();
  const RefPtr<nsIContent> content = chit.TextFrame()->GetContent();
  chit.NextWithinSubtree(nchars);
  nchars = chit.TextElementCharIndex() - charnum;

  RefPtr<nsFrameSelection> frameSelection = GetFrameSelection();

  frameSelection->HandleClick(content, charnum, charnum + nchars,
                              nsFrameSelection::FocusMode::kCollapseToNewPoint,
                              CARET_ASSOCIATE_BEFORE);
}

/**
 * For some content we cannot (or currently cannot) compute the length
 * without reflowing.  In those cases we need to fall back to using
 * GetSubStringLengthSlowFallback.
 *
 * We fall back for textPath since we need glyph positioning in order to
 * tell if any characters should be ignored due to having fallen off the
 * end of the textPath.
 *
 * We fall back for bidi because GetTrimmedOffsets does not produce the
 * correct results for bidi continuations when passed aPostReflow = false.
 * XXX It may be possible to determine which continuations to trim from (and
 * which sides), but currently we don't do that.  It would require us to
 * identify the visual (rather than logical) start and end of the line, to
 * avoid trimming at line-internal frame boundaries.  Maybe nsBidiPresUtils
 * methods like GetFrameToRightOf and GetFrameToLeftOf would help?
 *
 */
bool SVGTextFrame::RequiresSlowFallbackForSubStringLength() {
  TextFrameIterator frameIter(this);
  for (nsTextFrame* frame = frameIter.Current(); frame;
       frame = frameIter.Next()) {
    if (frameIter.TextPathFrame() || frame->GetNextContinuation()) {
      return true;
    }
  }
  return false;
}

/**
 * Implements the SVG DOM GetSubStringLength method for the specified
 * text content element.
 */
float SVGTextFrame::GetSubStringLengthFastPath(nsIContent* aContent,
                                               uint32_t charnum,
                                               uint32_t nchars,
                                               ErrorResult& aRv) {
  MOZ_ASSERT(!RequiresSlowFallbackForSubStringLength());

  // We only need our text correspondence to be up to date (no need to call
  // UpdateGlyphPositioning).
  TextNodeCorrespondenceRecorder::RecordCorrespondence(this);

  // Convert charnum/nchars from addressable characters relative to
  // aContent to global character indices.
  CharIterator chit(this, CharIterator::eAddressable, aContent,
                    /* aPostReflow */ false);
  if (!chit.AdvanceToSubtree() || !chit.Next(charnum) ||
      chit.IsAfterSubtree()) {
    aRv.ThrowIndexSizeError("Character index out of range");
    return 0;
  }

  // We do this after the ThrowIndexSizeError() bit so JS calls correctly throw
  // when necessary.
  if (nchars == 0) {
    return 0.0f;
  }

  charnum = chit.TextElementCharIndex();
  chit.NextWithinSubtree(nchars);
  nchars = chit.TextElementCharIndex() - charnum;

  // Sum of the substring advances.
  nscoord textLength = 0;

  TextFrameIterator frit(this);  // aSubtree = nullptr

  // Index of the first non-skipped char in the frame, and of a subsequent char
  // that we're interested in.  Both are relative to the index of the first
  // non-skipped char in the ancestor <text> element.
  uint32_t frameStartTextElementCharIndex = 0;
  uint32_t textElementCharIndex;

  for (nsTextFrame* frame = frit.Current(); frame; frame = frit.Next()) {
    frameStartTextElementCharIndex += frit.UndisplayedCharacters();
    textElementCharIndex = frameStartTextElementCharIndex;

    // Offset into frame's Text:
    const uint32_t untrimmedOffset = frame->GetContentOffset();
    const uint32_t untrimmedLength = frame->GetContentEnd() - untrimmedOffset;

    // Trim the offset/length to remove any leading/trailing white space.
    uint32_t trimmedOffset = untrimmedOffset;
    uint32_t trimmedLength = untrimmedLength;
    nsTextFrame::TrimmedOffsets trimmedOffsets = frame->GetTrimmedOffsets(
        frame->TextFragment(), nsTextFrame::TrimmedOffsetFlags::NotPostReflow);
    TrimOffsets(trimmedOffset, trimmedLength, trimmedOffsets);

    textElementCharIndex += trimmedOffset - untrimmedOffset;

    if (textElementCharIndex >= charnum + nchars) {
      break;  // we're past the end of the substring
    }

    uint32_t offset = textElementCharIndex;

    // Intersect the substring we are interested in with the range covered by
    // the nsTextFrame.
    IntersectInterval(offset, trimmedLength, charnum, nchars);

    if (trimmedLength != 0) {
      // Convert offset into an index into the frame.
      offset += trimmedOffset - textElementCharIndex;

      gfxSkipCharsIterator it = frame->EnsureTextRun(nsTextFrame::eInflated);
      gfxTextRun* textRun = frame->GetTextRun(nsTextFrame::eInflated);
      nsTextFrame::PropertyProvider provider(frame, it);

      Range range = ConvertOriginalToSkipped(it, offset, trimmedLength);

      // Accumulate the advance.
      textLength += textRun->GetAdvanceWidth(range, &provider);
    }

    // Advance, ready for next call:
    frameStartTextElementCharIndex += untrimmedLength;
  }

  nsPresContext* presContext = PresContext();
  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(
      presContext->AppUnitsPerDevPixel());

  return presContext->AppUnitsToGfxUnits(textLength) * cssPxPerDevPx /
         mFontSizeScaleFactor;
}

float SVGTextFrame::GetSubStringLengthSlowFallback(nsIContent* aContent,
                                                   uint32_t charnum,
                                                   uint32_t nchars,
                                                   ErrorResult& aRv) {
  UpdateGlyphPositioning();

  // Convert charnum/nchars from addressable characters relative to
  // aContent to global character indices.
  CharIterator chit(this, CharIterator::eAddressable, aContent);
  if (!chit.AdvanceToSubtree() || !chit.Next(charnum) ||
      chit.IsAfterSubtree()) {
    aRv.ThrowIndexSizeError("Character index out of range");
    return 0;
  }

  if (nchars == 0) {
    return 0.0f;
  }

  charnum = chit.TextElementCharIndex();
  chit.NextWithinSubtree(nchars);
  nchars = chit.TextElementCharIndex() - charnum;

  // Find each rendered run that intersects with the range defined
  // by charnum/nchars.
  nscoord textLength = 0;
  TextRenderedRunIterator runIter(this, TextRenderedRunIterator::eAllFrames);
  TextRenderedRun run = runIter.Current();
  while (run.mFrame) {
    // If this rendered run is past the substring we are interested in, we
    // are done.
    uint32_t offset = run.mTextElementCharIndex;
    if (offset >= charnum + nchars) {
      break;
    }

    // Intersect the substring we are interested in with the range covered by
    // the rendered run.
    uint32_t length = run.mTextFrameContentLength;
    IntersectInterval(offset, length, charnum, nchars);

    if (length != 0) {
      // Convert offset into an index into the frame.
      offset += run.mTextFrameContentOffset - run.mTextElementCharIndex;

      gfxSkipCharsIterator it =
          run.mFrame->EnsureTextRun(nsTextFrame::eInflated);
      gfxTextRun* textRun = run.mFrame->GetTextRun(nsTextFrame::eInflated);
      nsTextFrame::PropertyProvider provider(run.mFrame, it);

      Range range = ConvertOriginalToSkipped(it, offset, length);

      // Accumulate the advance.
      textLength += textRun->GetAdvanceWidth(range, &provider);
    }

    run = runIter.Next();
  }

  nsPresContext* presContext = PresContext();
  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(
      presContext->AppUnitsPerDevPixel());

  return presContext->AppUnitsToGfxUnits(textLength) * cssPxPerDevPx /
         mFontSizeScaleFactor;
}

/**
 * Implements the SVG DOM GetCharNumAtPosition method for the specified
 * text content element.
 */
int32_t SVGTextFrame::GetCharNumAtPosition(nsIContent* aContent,
                                           const DOMPointInit& aPoint) {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    // We're never reflowed if we're under a non-SVG element that is
    // never reflowed (such as the HTML 'caption' element).
    return -1;
  }

  UpdateGlyphPositioning();

  nsPresContext* context = PresContext();

  gfxPoint p(aPoint.mX, aPoint.mY);

  int32_t result = -1;

  TextRenderedRunIterator it(this, TextRenderedRunIterator::eAllFrames,
                             aContent);
  for (TextRenderedRun run = it.Current(); run.mFrame; run = it.Next()) {
    // Hit test this rendered run.  Later runs will override earlier ones.
    int32_t index = run.GetCharNumAtPosition(context, p);
    if (index != -1) {
      result = index + run.mTextElementCharIndex;
    }
  }

  if (result == -1) {
    return result;
  }

  return ConvertTextElementCharIndexToAddressableIndex(result, aContent);
}

/**
 * Implements the SVG DOM GetStartPositionOfChar method for the specified
 * text content element.
 */
already_AddRefed<DOMSVGPoint> SVGTextFrame::GetStartPositionOfChar(
    nsIContent* aContent, uint32_t aCharNum, ErrorResult& aRv) {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    // We're never reflowed if we're under a non-SVG element that is
    // never reflowed (such as the HTML 'caption' element).
    aRv.ThrowInvalidStateError("No layout information available for SVG text");
    return nullptr;
  }

  UpdateGlyphPositioning();

  CharIterator it(this, CharIterator::eAddressable, aContent);
  if (!it.AdvanceToSubtree() || !it.Next(aCharNum)) {
    aRv.ThrowIndexSizeError("Character index out of range");
    return nullptr;
  }

  // We need to return the start position of the whole glyph.
  uint32_t startIndex = it.GlyphStartTextElementCharIndex();

  RefPtr<DOMSVGPoint> point =
      new DOMSVGPoint(ToPoint(mPositions[startIndex].mPosition));
  return point.forget();
}

/**
 * Returns the advance of the entire glyph whose starting character is at
 * aTextElementCharIndex.
 *
 * aIterator, if provided, must be a CharIterator that already points to
 * aTextElementCharIndex that is restricted to aContent and is using
 * filter mode eAddressable.
 */
static gfxFloat GetGlyphAdvance(SVGTextFrame* aFrame, nsIContent* aContent,
                                uint32_t aTextElementCharIndex,
                                CharIterator* aIterator) {
  MOZ_ASSERT(!aIterator || (aIterator->Filter() == CharIterator::eAddressable &&
                            aIterator->GetSubtree() == aContent &&
                            aIterator->GlyphStartTextElementCharIndex() ==
                                aTextElementCharIndex),
             "Invalid aIterator");

  Maybe<CharIterator> newIterator;
  CharIterator* it = aIterator;
  if (!it) {
    newIterator.emplace(aFrame, CharIterator::eAddressable, aContent);
    if (!newIterator->AdvanceToSubtree()) {
      MOZ_ASSERT_UNREACHABLE("Invalid aContent");
      return 0.0;
    }
    it = newIterator.ptr();
  }

  while (it->GlyphStartTextElementCharIndex() != aTextElementCharIndex) {
    if (!it->Next()) {
      MOZ_ASSERT_UNREACHABLE("Invalid aTextElementCharIndex");
      return 0.0;
    }
  }

  if (it->AtEnd()) {
    MOZ_ASSERT_UNREACHABLE("Invalid aTextElementCharIndex");
    return 0.0;
  }

  nsPresContext* presContext = aFrame->PresContext();
  gfxFloat advance = 0.0;

  for (;;) {
    advance += it->GetAdvance(presContext);
    if (!it->Next() ||
        it->GlyphStartTextElementCharIndex() != aTextElementCharIndex) {
      break;
    }
  }

  return advance;
}

/**
 * Implements the SVG DOM GetEndPositionOfChar method for the specified
 * text content element.
 */
already_AddRefed<DOMSVGPoint> SVGTextFrame::GetEndPositionOfChar(
    nsIContent* aContent, uint32_t aCharNum, ErrorResult& aRv) {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    // We're never reflowed if we're under a non-SVG element that is
    // never reflowed (such as the HTML 'caption' element).
    aRv.ThrowInvalidStateError("No layout information available for SVG text");
    return nullptr;
  }

  UpdateGlyphPositioning();

  CharIterator it(this, CharIterator::eAddressable, aContent);
  if (!it.AdvanceToSubtree() || !it.Next(aCharNum)) {
    aRv.ThrowIndexSizeError("Character index out of range");
    return nullptr;
  }

  // We need to return the end position of the whole glyph.
  uint32_t startIndex = it.GlyphStartTextElementCharIndex();

  // Get the advance of the glyph.
  gfxFloat advance =
      GetGlyphAdvance(this, aContent, startIndex,
                      it.IsClusterAndLigatureGroupStart() ? &it : nullptr);
  if (it.TextRun()->IsRightToLeft()) {
    advance = -advance;
  }

  // The end position is the start position plus the advance in the direction
  // of the glyph's rotation.
  Matrix m = Matrix::Rotation(mPositions[startIndex].mAngle) *
             Matrix::Translation(ToPoint(mPositions[startIndex].mPosition));
  Point p = m.TransformPoint(Point(advance / mFontSizeScaleFactor, 0));

  RefPtr<DOMSVGPoint> point = new DOMSVGPoint(p);
  return point.forget();
}

/**
 * Implements the SVG DOM GetExtentOfChar method for the specified
 * text content element.
 */
already_AddRefed<SVGRect> SVGTextFrame::GetExtentOfChar(nsIContent* aContent,
                                                        uint32_t aCharNum,
                                                        ErrorResult& aRv) {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    // We're never reflowed if we're under a non-SVG element that is
    // never reflowed (such as the HTML 'caption' element).
    aRv.ThrowInvalidStateError("No layout information available for SVG text");
    return nullptr;
  }

  UpdateGlyphPositioning();

  // Search for the character whose addressable index is aCharNum.
  CharIterator it(this, CharIterator::eAddressable, aContent);
  if (!it.AdvanceToSubtree() || !it.Next(aCharNum)) {
    aRv.ThrowIndexSizeError("Character index out of range");
    return nullptr;
  }

  nsPresContext* presContext = PresContext();
  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(
      presContext->AppUnitsPerDevPixel());

  nsTextFrame* textFrame = it.TextFrame();
  uint32_t startIndex = it.GlyphStartTextElementCharIndex();
  bool isRTL = it.TextRun()->IsRightToLeft();
  bool isVertical = it.TextRun()->IsVertical();

  // Get the glyph advance.
  gfxFloat advance =
      GetGlyphAdvance(this, aContent, startIndex,
                      it.IsClusterAndLigatureGroupStart() ? &it : nullptr);
  gfxFloat x = isRTL ? -advance : 0.0;

  // The ascent and descent gives the height of the glyph.
  gfxFloat ascent, descent;
  GetAscentAndDescentInAppUnits(textFrame, ascent, descent);

  // The horizontal extent is the origin of the glyph plus the advance
  // in the direction of the glyph's rotation.
  gfxMatrix m;
  m.PreTranslate(mPositions[startIndex].mPosition);
  m.PreRotate(mPositions[startIndex].mAngle);
  m.PreScale(1 / mFontSizeScaleFactor, 1 / mFontSizeScaleFactor);

  gfxRect glyphRect;
  if (isVertical) {
    glyphRect = gfxRect(
        -presContext->AppUnitsToGfxUnits(descent) * cssPxPerDevPx, x,
        presContext->AppUnitsToGfxUnits(ascent + descent) * cssPxPerDevPx,
        advance);
  } else {
    glyphRect = gfxRect(
        x, -presContext->AppUnitsToGfxUnits(ascent) * cssPxPerDevPx, advance,
        presContext->AppUnitsToGfxUnits(ascent + descent) * cssPxPerDevPx);
  }

  // Transform the glyph's rect into user space.
  gfxRect r = m.TransformBounds(glyphRect);

  RefPtr<SVGRect> rect = new SVGRect(aContent, ToRect(r));
  return rect.forget();
}

/**
 * Implements the SVG DOM GetRotationOfChar method for the specified
 * text content element.
 */
float SVGTextFrame::GetRotationOfChar(nsIContent* aContent, uint32_t aCharNum,
                                      ErrorResult& aRv) {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid->IsSubtreeDirty()) {
    // We're never reflowed if we're under a non-SVG element that is
    // never reflowed (such as the HTML 'caption' element).
    aRv.ThrowInvalidStateError("No layout information available for SVG text");
    return 0;
  }

  UpdateGlyphPositioning();

  CharIterator it(this, CharIterator::eAddressable, aContent);
  if (!it.AdvanceToSubtree() || !it.Next(aCharNum)) {
    aRv.ThrowIndexSizeError("Character index out of range");
    return 0;
  }

  // we need to account for the glyph's underlying orientation
  const gfxTextRun::GlyphRun& glyphRun = it.GlyphRun();
  int32_t glyphOrientation =
      90 * (glyphRun.IsSidewaysRight() - glyphRun.IsSidewaysLeft());

  return mPositions[it.TextElementCharIndex()].mAngle * 180.0 / M_PI +
         glyphOrientation;
}

//----------------------------------------------------------------------
// SVGTextFrame text layout methods

/**
 * Given the character position array before values have been filled in
 * to any unspecified positions, and an array of dx/dy values, returns whether
 * a character at a given index should start a new rendered run.
 *
 * @param aPositions The array of character positions before unspecified
 *   positions have been filled in and dx/dy values have been added to them.
 * @param aDeltas The array of dx/dy values.
 * @param aIndex The character index in question.
 */
static bool ShouldStartRunAtIndex(const nsTArray<CharPosition>& aPositions,
                                  const nsTArray<gfxPoint>& aDeltas,
                                  uint32_t aIndex) {
  if (aIndex == 0) {
    return true;
  }

  if (aIndex < aPositions.Length()) {
    // If an explicit x or y value was given, start a new run.
    if (aPositions[aIndex].IsXSpecified() ||
        aPositions[aIndex].IsYSpecified()) {
      return true;
    }

    // If a non-zero rotation was given, or the previous character had a non-
    // zero rotation, start a new run.
    if ((aPositions[aIndex].IsAngleSpecified() &&
         aPositions[aIndex].mAngle != 0.0f) ||
        (aPositions[aIndex - 1].IsAngleSpecified() &&
         (aPositions[aIndex - 1].mAngle != 0.0f))) {
      return true;
    }
  }

  if (aIndex < aDeltas.Length()) {
    // If a non-zero dx or dy value was given, start a new run.
    if (aDeltas[aIndex].x != 0.0 || aDeltas[aIndex].y != 0.0) {
      return true;
    }
  }

  return false;
}

bool SVGTextFrame::ResolvePositionsForNode(nsIContent* aContent,
                                           uint32_t& aIndex, bool aInTextPath,
                                           bool& aForceStartOfChunk,
                                           nsTArray<gfxPoint>& aDeltas) {
  if (aContent->IsText()) {
    // We found a text node.
    uint32_t length = aContent->AsText()->TextLength();
    if (length) {
      uint32_t end = aIndex + length;
      if (MOZ_UNLIKELY(end > mPositions.Length())) {
        MOZ_ASSERT_UNREACHABLE(
            "length of mPositions does not match characters "
            "found by iterating content");
        return false;
      }
      if (aForceStartOfChunk) {
        // Note this character as starting a new anchored chunk.
        mPositions[aIndex].mStartOfChunk = true;
        aForceStartOfChunk = false;
      }
      while (aIndex < end) {
        // Record whether each of these characters should start a new rendered
        // run.  That is always the case for characters on a text path.
        //
        // Run boundaries due to rotate="" values are handled in
        // DoGlyphPositioning.
        if (aInTextPath || ShouldStartRunAtIndex(mPositions, aDeltas, aIndex)) {
          mPositions[aIndex].mRunBoundary = true;
        }
        aIndex++;
      }
    }
    return true;
  }

  // Skip past elements that aren't text content elements.
  if (!IsTextContentElement(aContent)) {
    return true;
  }

  if (aContent->IsSVGElement(nsGkAtoms::textPath)) {
    // Any ‘y’ attributes on horizontal <textPath> elements are ignored.
    // Similarly, for vertical <texPath>s x attributes are ignored.
    // <textPath> elements behave as if they have x="0" y="0" on them, but only
    // if there is not a value for the non-ignored coordinate that got inherited
    // from a parent.  We skip this if there is no text content, so that empty
    // <textPath>s don't interrupt the layout of text in the parent element.
    if (HasTextContent(aContent)) {
      if (MOZ_UNLIKELY(aIndex >= mPositions.Length())) {
        MOZ_ASSERT_UNREACHABLE(
            "length of mPositions does not match characters "
            "found by iterating content");
        return false;
      }
      bool vertical = GetWritingMode().IsVertical();
      if (vertical || !mPositions[aIndex].IsXSpecified()) {
        mPositions[aIndex].mPosition.x = 0.0;
      }
      if (!vertical || !mPositions[aIndex].IsYSpecified()) {
        mPositions[aIndex].mPosition.y = 0.0;
      }
      mPositions[aIndex].mStartOfChunk = true;
    }
  } else if (!aContent->IsSVGElement(nsGkAtoms::a)) {
    MOZ_ASSERT(aContent->IsSVGElement());

    // We have a text content element that can have x/y/dx/dy/rotate attributes.
    SVGElement* element = static_cast<SVGElement*>(aContent);

    // Get x, y, dx, dy.
    SVGUserUnitList x, y, dx, dy;
    element->GetAnimatedLengthListValues(&x, &y, &dx, &dy, nullptr);

    // Get rotate.
    const SVGNumberList* rotate = nullptr;
    SVGAnimatedNumberList* animatedRotate =
        element->GetAnimatedNumberList(nsGkAtoms::rotate);
    if (animatedRotate) {
      rotate = &animatedRotate->GetAnimValue();
    }

    bool percentages = false;
    uint32_t count = GetTextContentLength(aContent);

    if (MOZ_UNLIKELY(aIndex + count > mPositions.Length())) {
      MOZ_ASSERT_UNREACHABLE(
          "length of mPositions does not match characters "
          "found by iterating content");
      return false;
    }

    // New text anchoring chunks start at each character assigned a position
    // with x="" or y="", or if we forced one with aForceStartOfChunk due to
    // being just after a <textPath>.
    uint32_t newChunkCount = std::max(x.Length(), y.Length());
    if (!newChunkCount && aForceStartOfChunk) {
      newChunkCount = 1;
    }
    for (uint32_t i = 0, j = 0; i < newChunkCount && j < count; j++) {
      if (!mPositions[aIndex + j].mUnaddressable) {
        mPositions[aIndex + j].mStartOfChunk = true;
        i++;
      }
    }

    // Copy dx="" and dy="" values into aDeltas.
    if (!dx.IsEmpty() || !dy.IsEmpty()) {
      // Any unspecified deltas when we grow the array just get left as 0s.
      aDeltas.EnsureLengthAtLeast(aIndex + count);
      for (uint32_t i = 0, j = 0; i < dx.Length() && j < count; j++) {
        if (!mPositions[aIndex + j].mUnaddressable) {
          aDeltas[aIndex + j].x = dx[i];
          percentages = percentages || dx.HasPercentageValueAt(i);
          i++;
        }
      }
      for (uint32_t i = 0, j = 0; i < dy.Length() && j < count; j++) {
        if (!mPositions[aIndex + j].mUnaddressable) {
          aDeltas[aIndex + j].y = dy[i];
          percentages = percentages || dy.HasPercentageValueAt(i);
          i++;
        }
      }
    }

    // Copy x="" and y="" values.
    for (uint32_t i = 0, j = 0; i < x.Length() && j < count; j++) {
      if (!mPositions[aIndex + j].mUnaddressable) {
        mPositions[aIndex + j].mPosition.x = x[i];
        percentages = percentages || x.HasPercentageValueAt(i);
        i++;
      }
    }
    for (uint32_t i = 0, j = 0; i < y.Length() && j < count; j++) {
      if (!mPositions[aIndex + j].mUnaddressable) {
        mPositions[aIndex + j].mPosition.y = y[i];
        percentages = percentages || y.HasPercentageValueAt(i);
        i++;
      }
    }

    // Copy rotate="" values.
    if (rotate && !rotate->IsEmpty()) {
      uint32_t i = 0, j = 0;
      while (i < rotate->Length() && j < count) {
        if (!mPositions[aIndex + j].mUnaddressable) {
          mPositions[aIndex + j].mAngle = M_PI * (*rotate)[i] / 180.0;
          i++;
        }
        j++;
      }
      // Propagate final rotate="" value to the end of this element.
      while (j < count) {
        mPositions[aIndex + j].mAngle = mPositions[aIndex + j - 1].mAngle;
        j++;
      }
    }

    if (percentages) {
      AddStateBits(NS_STATE_SVG_POSITIONING_MAY_USE_PERCENTAGES);
    }
  }

  // Recurse to children.
  bool inTextPath = aInTextPath || aContent->IsSVGElement(nsGkAtoms::textPath);
  for (nsIContent* child = aContent->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    bool ok = ResolvePositionsForNode(child, aIndex, inTextPath,
                                      aForceStartOfChunk, aDeltas);
    if (!ok) {
      return false;
    }
  }

  if (aContent->IsSVGElement(nsGkAtoms::textPath)) {
    // Force a new anchored chunk just after a <textPath>.
    aForceStartOfChunk = true;
  }

  return true;
}

bool SVGTextFrame::ResolvePositions(nsTArray<gfxPoint>& aDeltas,
                                    bool aRunPerGlyph) {
  NS_ASSERTION(mPositions.IsEmpty(), "expected mPositions to be empty");
  RemoveStateBits(NS_STATE_SVG_POSITIONING_MAY_USE_PERCENTAGES);

  CharIterator it(this, CharIterator::eOriginal, /* aSubtree */ nullptr);
  if (it.AtEnd()) {
    return false;
  }

  // We assume the first character position is (0,0) unless we later see
  // otherwise, and note it as unaddressable if it is.
  bool firstCharUnaddressable = it.IsOriginalCharUnaddressable();
  mPositions.AppendElement(CharPosition::Unspecified(firstCharUnaddressable));

  // Fill in unspecified positions for all remaining characters, noting
  // them as unaddressable if they are.
  uint32_t index = 0;
  while (it.Next()) {
    while (++index < it.TextElementCharIndex()) {
      mPositions.AppendElement(CharPosition::Unspecified(false));
    }
    mPositions.AppendElement(
        CharPosition::Unspecified(it.IsOriginalCharUnaddressable()));
  }
  while (++index < it.TextElementCharIndex()) {
    mPositions.AppendElement(CharPosition::Unspecified(false));
  }

  // Recurse over the content and fill in character positions as we go.
  bool forceStartOfChunk = false;
  index = 0;
  bool ok = ResolvePositionsForNode(mContent, index, aRunPerGlyph,
                                    forceStartOfChunk, aDeltas);
  return ok && index > 0;
}

void SVGTextFrame::DetermineCharPositions(nsTArray<nsPoint>& aPositions) {
  NS_ASSERTION(aPositions.IsEmpty(), "expected aPositions to be empty");

  nsPoint position;

  TextFrameIterator frit(this);
  for (nsTextFrame* frame = frit.Current(); frame; frame = frit.Next()) {
    gfxSkipCharsIterator it = frame->EnsureTextRun(nsTextFrame::eInflated);
    gfxTextRun* textRun = frame->GetTextRun(nsTextFrame::eInflated);
    nsTextFrame::PropertyProvider provider(frame, it);

    // Reset the position to the new frame's position.
    position = frit.Position();
    if (textRun->IsVertical()) {
      if (textRun->IsRightToLeft()) {
        position.y += frame->GetRect().height;
      }
      position.x += GetBaselinePosition(frame, textRun, frit.DominantBaseline(),
                                        mFontSizeScaleFactor);
    } else {
      if (textRun->IsRightToLeft()) {
        position.x += frame->GetRect().width;
      }
      position.y += GetBaselinePosition(frame, textRun, frit.DominantBaseline(),
                                        mFontSizeScaleFactor);
    }

    // Any characters not in a frame, e.g. when display:none.
    for (uint32_t i = 0; i < frit.UndisplayedCharacters(); i++) {
      aPositions.AppendElement(position);
    }

    // Any white space characters trimmed at the start of the line of text.
    nsTextFrame::TrimmedOffsets trimmedOffsets =
        frame->GetTrimmedOffsets(frame->TextFragment());
    while (it.GetOriginalOffset() < trimmedOffsets.mStart) {
      aPositions.AppendElement(position);
      it.AdvanceOriginal(1);
    }

    // Visible characters in the text frame.
    while (it.GetOriginalOffset() < frame->GetContentEnd()) {
      aPositions.AppendElement(position);
      if (!it.IsOriginalCharSkipped()) {
        // Accumulate partial ligature advance into position.  (We must get
        // partial advances rather than get the advance of the whole ligature
        // group / cluster at once, since the group may span text frames, and
        // the PropertyProvider only has spacing information for the current
        // text frame.)
        uint32_t offset = it.GetSkippedOffset();
        nscoord advance =
            textRun->GetAdvanceWidth(Range(offset, offset + 1), &provider);
        (textRun->IsVertical() ? position.y : position.x) +=
            textRun->IsRightToLeft() ? -advance : advance;
      }
      it.AdvanceOriginal(1);
    }
  }

  // Finally any characters at the end that are not in a frame.
  for (uint32_t i = 0; i < frit.UndisplayedCharacters(); i++) {
    aPositions.AppendElement(position);
  }
}

/**
 * Physical text-anchor values.
 */
enum TextAnchorSide { eAnchorLeft, eAnchorMiddle, eAnchorRight };

/**
 * Converts a logical text-anchor value to its physical value, based on whether
 * it is for an RTL frame.
 */
static TextAnchorSide ConvertLogicalTextAnchorToPhysical(
    StyleTextAnchor aTextAnchor, bool aIsRightToLeft) {
  NS_ASSERTION(uint8_t(aTextAnchor) <= 3, "unexpected value for aTextAnchor");
  if (!aIsRightToLeft) {
    return TextAnchorSide(uint8_t(aTextAnchor));
  }
  return TextAnchorSide(2 - uint8_t(aTextAnchor));
}

/**
 * Shifts the recorded character positions for an anchored chunk.
 *
 * @param aCharPositions The recorded character positions.
 * @param aChunkStart The character index the starts the anchored chunk.  This
 *   character's initial position is the anchor point.
 * @param aChunkEnd The character index just after the end of the anchored
 *   chunk.
 * @param aVisIStartEdge The left/top-most edge of any of the glyphs within the
 *   anchored chunk.
 * @param aVisIEndEdge The right/bottom-most edge of any of the glyphs within
 *   the anchored chunk.
 * @param aAnchorSide The direction to anchor.
 */
static void ShiftAnchoredChunk(nsTArray<CharPosition>& aCharPositions,
                               uint32_t aChunkStart, uint32_t aChunkEnd,
                               gfxFloat aVisIStartEdge, gfxFloat aVisIEndEdge,
                               TextAnchorSide aAnchorSide, bool aVertical) {
  NS_ASSERTION(aVisIStartEdge <= aVisIEndEdge,
               "unexpected anchored chunk edges");
  NS_ASSERTION(aChunkStart < aChunkEnd,
               "unexpected values for aChunkStart and aChunkEnd");

  gfxFloat shift = aVertical ? aCharPositions[aChunkStart].mPosition.y
                             : aCharPositions[aChunkStart].mPosition.x;
  switch (aAnchorSide) {
    case eAnchorLeft:
      shift -= aVisIStartEdge;
      break;
    case eAnchorMiddle:
      shift -= (aVisIStartEdge + aVisIEndEdge) / 2;
      break;
    case eAnchorRight:
      shift -= aVisIEndEdge;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected value for aAnchorSide");
  }

  if (shift != 0.0) {
    if (aVertical) {
      for (uint32_t i = aChunkStart; i < aChunkEnd; i++) {
        aCharPositions[i].mPosition.y += shift;
      }
    } else {
      for (uint32_t i = aChunkStart; i < aChunkEnd; i++) {
        aCharPositions[i].mPosition.x += shift;
      }
    }
  }
}

void SVGTextFrame::AdjustChunksForLineBreaks() {
  nsBlockFrame* block = do_QueryFrame(PrincipalChildList().FirstChild());
  NS_ASSERTION(block, "expected block frame");

  nsBlockFrame::LineIterator line = block->LinesBegin();

  CharIterator it(this, CharIterator::eOriginal, /* aSubtree */ nullptr);
  while (!it.AtEnd() && line != block->LinesEnd()) {
    if (it.TextFrame() == line->mFirstChild) {
      mPositions[it.TextElementCharIndex()].mStartOfChunk = true;
      line++;
    }
    it.AdvancePastCurrentFrame();
  }
}

void SVGTextFrame::AdjustPositionsForClusters() {
  nsPresContext* presContext = PresContext();

  // Find all of the characters that are in the middle of a cluster or
  // ligature group, and adjust their positions and rotations to match
  // the first character of the cluster/group.
  //
  // Also move the boundaries of text rendered runs and anchored chunks to
  // not lie in the middle of cluster/group.

  // The partial advance of the current cluster or ligature group that we
  // have accumulated.
  gfxFloat partialAdvance = 0.0;

  CharIterator it(this, CharIterator::eUnskipped, /* aSubtree */ nullptr);
  while (!it.AtEnd()) {
    if (it.IsClusterAndLigatureGroupStart()) {
      // If we're at the start of a new cluster or ligature group, reset our
      // accumulated partial advance.
      partialAdvance = 0.0;
    } else {
      // Otherwise, we're in the middle of a cluster or ligature group, and
      // we need to use the currently accumulated partial advance to adjust
      // the character's position and rotation.

      // Find the start of the cluster/ligature group.
      uint32_t charIndex = it.TextElementCharIndex();
      uint32_t startIndex = it.GlyphStartTextElementCharIndex();
      MOZ_ASSERT(charIndex != startIndex,
                 "If the current character is in the middle of a cluster or "
                 "ligature group, then charIndex must be different from "
                 "startIndex");

      mPositions[charIndex].mClusterOrLigatureGroupMiddle = true;

      // Don't allow different rotations on ligature parts.
      bool rotationAdjusted = false;
      double angle = mPositions[startIndex].mAngle;
      if (mPositions[charIndex].mAngle != angle) {
        mPositions[charIndex].mAngle = angle;
        rotationAdjusted = true;
      }

      // Update the character position.
      gfxFloat advance = partialAdvance / mFontSizeScaleFactor;
      gfxPoint direction = gfxPoint(cos(angle), sin(angle)) *
                           (it.TextRun()->IsRightToLeft() ? -1.0 : 1.0);
      if (it.TextRun()->IsVertical()) {
        std::swap(direction.x, direction.y);
      }
      mPositions[charIndex].mPosition =
          mPositions[startIndex].mPosition + direction * advance;

      // Ensure any runs that would end in the middle of a ligature now end just
      // after the ligature.
      if (mPositions[charIndex].mRunBoundary) {
        mPositions[charIndex].mRunBoundary = false;
        if (charIndex + 1 < mPositions.Length()) {
          mPositions[charIndex + 1].mRunBoundary = true;
        }
      } else if (rotationAdjusted) {
        if (charIndex + 1 < mPositions.Length()) {
          mPositions[charIndex + 1].mRunBoundary = true;
        }
      }

      // Ensure any anchored chunks that would begin in the middle of a ligature
      // now begin just after the ligature.
      if (mPositions[charIndex].mStartOfChunk) {
        mPositions[charIndex].mStartOfChunk = false;
        if (charIndex + 1 < mPositions.Length()) {
          mPositions[charIndex + 1].mStartOfChunk = true;
        }
      }
    }

    // Accumulate the current character's partial advance.
    partialAdvance += it.GetAdvance(presContext);

    it.Next();
  }
}

already_AddRefed<Path> SVGTextFrame::GetTextPath(nsIFrame* aTextPathFrame) {
  nsIContent* content = aTextPathFrame->GetContent();
  SVGTextPathElement* tp = static_cast<SVGTextPathElement*>(content);
  if (tp->mPath.IsRendered()) {
    // This is just an attribute so there's no transform that can apply
    // so we can just return the path directly.
    return tp->mPath.GetAnimValue().BuildPathForMeasuring();
  }

  SVGGeometryElement* geomElement =
      SVGObserverUtils::GetAndObserveTextPathsPath(aTextPathFrame);
  if (!geomElement) {
    return nullptr;
  }

  RefPtr<Path> path = geomElement->GetOrBuildPathForMeasuring();
  if (!path) {
    return nullptr;
  }

  gfxMatrix matrix = geomElement->PrependLocalTransformsTo(gfxMatrix());
  if (!matrix.IsIdentity()) {
    // Apply the geometry element's transform
    RefPtr<PathBuilder> builder =
        path->TransformedCopyToBuilder(ToMatrix(matrix));
    path = builder->Finish();
  }

  return path.forget();
}

gfxFloat SVGTextFrame::GetOffsetScale(nsIFrame* aTextPathFrame) {
  nsIContent* content = aTextPathFrame->GetContent();
  SVGTextPathElement* tp = static_cast<SVGTextPathElement*>(content);
  if (tp->mPath.IsRendered()) {
    // A path attribute has no pathLength or transform
    // so we return a unit scale.
    return 1.0;
  }

  SVGGeometryElement* geomElement =
      SVGObserverUtils::GetAndObserveTextPathsPath(aTextPathFrame);
  if (!geomElement) {
    return 1.0;
  }
  return geomElement->GetPathLengthScale(SVGGeometryElement::eForTextPath);
}

gfxFloat SVGTextFrame::GetStartOffset(nsIFrame* aTextPathFrame) {
  SVGTextPathElement* tp =
      static_cast<SVGTextPathElement*>(aTextPathFrame->GetContent());
  SVGAnimatedLength* length =
      &tp->mLengthAttributes[SVGTextPathElement::STARTOFFSET];

  if (length->IsPercentage()) {
    if (!std::isfinite(GetOffsetScale(aTextPathFrame))) {
      // Either pathLength="0" for this path or the path has 0 length.
      return 0.0;
    }
    RefPtr<Path> data = GetTextPath(aTextPathFrame);
    return data ? length->GetAnimValInSpecifiedUnits() * data->ComputeLength() /
                      100.0
                : 0.0;
  }
  float lengthValue = length->GetAnimValue(tp);
  // If offsetScale is infinity we want to return 0 not NaN
  return lengthValue == 0 ? 0.0 : lengthValue * GetOffsetScale(aTextPathFrame);
}

void SVGTextFrame::DoTextPathLayout() {
  nsPresContext* context = PresContext();

  CharIterator it(this, CharIterator::eOriginal, /* aSubtree */ nullptr);
  while (!it.AtEnd()) {
    nsIFrame* textPathFrame = it.TextPathFrame();
    if (!textPathFrame) {
      // Skip past this frame if we're not in a text path.
      it.AdvancePastCurrentFrame();
      continue;
    }

    // Get the path itself.
    RefPtr<Path> path = GetTextPath(textPathFrame);
    if (!path) {
      uint32_t start = it.TextElementCharIndex();
      it.AdvancePastCurrentTextPathFrame();
      uint32_t end = it.TextElementCharIndex();
      for (uint32_t i = start; i < end; i++) {
        mPositions[i].mHidden = true;
      }
      continue;
    }

    SVGTextPathElement* textPath =
        static_cast<SVGTextPathElement*>(textPathFrame->GetContent());
    uint16_t side =
        textPath->EnumAttributes()[SVGTextPathElement::SIDE].GetAnimValue();

    gfxFloat offset = GetStartOffset(textPathFrame);
    Float pathLength = path->ComputeLength();

    // If the first character within the text path is in the middle of a
    // cluster or ligature group, just skip it and don't apply text path
    // positioning.
    while (!it.AtEnd()) {
      if (it.IsOriginalCharSkipped()) {
        it.Next();
        continue;
      }
      if (it.IsClusterAndLigatureGroupStart()) {
        break;
      }
      it.Next();
    }

    bool skippedEndOfTextPath = false;

    // Loop for each character in the text path.
    while (!it.AtEnd() && it.TextPathFrame() &&
           it.TextPathFrame()->GetContent() == textPath) {
      // The index of the cluster or ligature group's first character.
      uint32_t i = it.TextElementCharIndex();

      // The index of the next character of the cluster or ligature.
      // We track this as we loop over the characters below so that we
      // can detect undisplayed characters and append entries into
      // partialAdvances for them.
      uint32_t j = i + 1;

      MOZ_ASSERT(!mPositions[i].mClusterOrLigatureGroupMiddle);

      gfxFloat sign = it.TextRun()->IsRightToLeft() ? -1.0 : 1.0;
      bool vertical = it.TextRun()->IsVertical();

      // Compute cumulative advances for each character of the cluster or
      // ligature group.
      AutoTArray<gfxFloat, 4> partialAdvances;
      gfxFloat partialAdvance = it.GetAdvance(context);
      partialAdvances.AppendElement(partialAdvance);
      while (it.Next()) {
        // Append entries for any undisplayed characters the CharIterator
        // skipped over.
        MOZ_ASSERT(j <= it.TextElementCharIndex());
        while (j < it.TextElementCharIndex()) {
          partialAdvances.AppendElement(partialAdvance);
          ++j;
        }
        // This loop may end up outside of the current text path, but
        // that's OK; we'll consider any complete cluster or ligature
        // group that begins inside the text path as being affected
        // by it.
        if (it.IsOriginalCharSkipped()) {
          if (!it.TextPathFrame()) {
            skippedEndOfTextPath = true;
            break;
          }
          // Leave partialAdvance unchanged.
        } else if (it.IsClusterAndLigatureGroupStart()) {
          break;
        } else {
          partialAdvance += it.GetAdvance(context);
        }
        partialAdvances.AppendElement(partialAdvance);
      }

      if (!skippedEndOfTextPath) {
        // Any final undisplayed characters the CharIterator skipped over.
        MOZ_ASSERT(j <= it.TextElementCharIndex());
        while (j < it.TextElementCharIndex()) {
          partialAdvances.AppendElement(partialAdvance);
          ++j;
        }
      }

      gfxFloat halfAdvance =
          partialAdvances.LastElement() / mFontSizeScaleFactor / 2.0;
      gfxFloat midx =
          (vertical ? mPositions[i].mPosition.y : mPositions[i].mPosition.x) +
          sign * halfAdvance + offset;

      // Hide the character if it falls off the end of the path.
      mPositions[i].mHidden = midx < 0 || midx > pathLength;

      // Position the character on the path at the right angle.
      Point tangent;  // Unit vector tangent to the point we find.
      Point pt;
      if (side == TEXTPATH_SIDETYPE_RIGHT) {
        pt = path->ComputePointAtLength(Float(pathLength - midx), &tangent);
        tangent = -tangent;
      } else {
        pt = path->ComputePointAtLength(Float(midx), &tangent);
      }
      Float rotation = vertical ? atan2f(-tangent.x, tangent.y)
                                : atan2f(tangent.y, tangent.x);
      Point normal(-tangent.y, tangent.x);  // Unit vector normal to the point.
      Point offsetFromPath = normal * (vertical ? -mPositions[i].mPosition.x
                                                : mPositions[i].mPosition.y);
      pt += offsetFromPath;
      Point direction = tangent * sign;
      mPositions[i].mPosition =
          ThebesPoint(pt) - ThebesPoint(direction) * halfAdvance;
      mPositions[i].mAngle += rotation;

      // Position any characters for a partial ligature.
      for (uint32_t k = i + 1; k < j; k++) {
        gfxPoint partialAdvance = ThebesPoint(direction) *
                                  partialAdvances[k - i] / mFontSizeScaleFactor;
        mPositions[k].mPosition = mPositions[i].mPosition + partialAdvance;
        mPositions[k].mAngle = mPositions[i].mAngle;
        mPositions[k].mHidden = mPositions[i].mHidden;
      }
    }
  }
}

void SVGTextFrame::DoAnchoring() {
  nsPresContext* presContext = PresContext();

  CharIterator it(this, CharIterator::eOriginal, /* aSubtree */ nullptr);

  // Don't need to worry about skipped or trimmed characters.
  while (!it.AtEnd() &&
         (it.IsOriginalCharSkipped() || it.IsOriginalCharTrimmed())) {
    it.Next();
  }

  bool vertical = GetWritingMode().IsVertical();
  uint32_t start = it.TextElementCharIndex();
  while (start < mPositions.Length()) {
    it.AdvanceToCharacter(start);
    nsTextFrame* chunkFrame = it.TextFrame();

    // Measure characters in this chunk to find the left-most and right-most
    // edges of all glyphs within the chunk.
    uint32_t index = it.TextElementCharIndex();
    uint32_t end = start;
    gfxFloat left = std::numeric_limits<gfxFloat>::infinity();
    gfxFloat right = -std::numeric_limits<gfxFloat>::infinity();
    do {
      if (!it.IsOriginalCharSkipped() && !it.IsOriginalCharTrimmed()) {
        gfxFloat advance = it.GetAdvance(presContext) / mFontSizeScaleFactor;
        gfxFloat pos = it.TextRun()->IsVertical()
                           ? mPositions[index].mPosition.y
                           : mPositions[index].mPosition.x;
        if (it.TextRun()->IsRightToLeft()) {
          left = std::min(left, pos - advance);
          right = std::max(right, pos);
        } else {
          left = std::min(left, pos);
          right = std::max(right, pos + advance);
        }
      }
      it.Next();
      index = end = it.TextElementCharIndex();
    } while (!it.AtEnd() && !mPositions[end].mStartOfChunk);

    if (left != std::numeric_limits<gfxFloat>::infinity()) {
      bool isRTL =
          chunkFrame->StyleVisibility()->mDirection == StyleDirection::Rtl;
      TextAnchorSide anchor = ConvertLogicalTextAnchorToPhysical(
          chunkFrame->StyleSVG()->mTextAnchor, isRTL);

      ShiftAnchoredChunk(mPositions, start, end, left, right, anchor, vertical);
    }

    start = it.TextElementCharIndex();
  }
}

void SVGTextFrame::DoGlyphPositioning() {
  mPositions.Clear();
  RemoveStateBits(NS_STATE_SVG_POSITIONING_DIRTY);

  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (kid && kid->IsSubtreeDirty()) {
    MOZ_ASSERT(false, "should have already reflowed the kid");
    return;
  }

  if (kid && HasAnyStateBits(NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY) &&
      kid->HasAnyStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION) &&
      PresContext()->BidiEnabled()) {
    // Resolving bidi creates continuation frames that haven't been reflowed,
    // so wait for the next reflow to do that.
    AddStateBits(NS_STATE_SVG_POSITIONING_DIRTY);
    return;
  }

  // Since we can be called directly via GetBBoxContribution, our correspondence
  // may not be up to date.
  TextNodeCorrespondenceRecorder::RecordCorrespondence(this);

  // Determine the positions of each character in app units.
  AutoTArray<nsPoint, 64> charPositions;
  DetermineCharPositions(charPositions);

  if (charPositions.IsEmpty()) {
    // No characters, so nothing to do.
    return;
  }

  // If the textLength="" attribute was specified, then we need ResolvePositions
  // to record that a new run starts with each glyph.
  SVGTextContentElement* element =
      static_cast<SVGTextContentElement*>(GetContent());
  SVGAnimatedLength* textLengthAttr =
      element->GetAnimatedLength(nsGkAtoms::textLength);
  uint16_t lengthAdjust =
      element->EnumAttributes()[SVGTextContentElement::LENGTHADJUST]
          .GetAnimValue();
  bool adjustingTextLength = textLengthAttr->IsExplicitlySet();
  float expectedTextLength = textLengthAttr->GetAnimValue(element);

  if (adjustingTextLength &&
      (expectedTextLength < 0.0f || lengthAdjust == LENGTHADJUST_UNKNOWN)) {
    // If textLength="" is less than zero or lengthAdjust is unknown, ignore it.
    adjustingTextLength = false;
  }

  // Get the x, y, dx, dy, rotate values for the subtree.
  AutoTArray<gfxPoint, 16> deltas;
  if (!ResolvePositions(deltas, adjustingTextLength)) {
    // If ResolvePositions returned false, it means either there were some
    // characters in the DOM but none of them are displayed, or there was
    // an error in processing mPositions.  Clear out mPositions so that we don't
    // attempt to do any painting later.
    mPositions.Clear();
    return;
  }

  // XXX We might be able to do less work when there is at most a single
  // x/y/dx/dy position.

  // Truncate the positioning arrays to the actual number of characters present.
  TruncateTo(deltas, charPositions);
  TruncateTo(mPositions, charPositions);

  // Fill in an unspecified character position at index 0.
  if (!mPositions[0].IsXSpecified()) {
    mPositions[0].mPosition.x = 0.0;
  }
  if (!mPositions[0].IsYSpecified()) {
    mPositions[0].mPosition.y = 0.0;
  }
  if (!mPositions[0].IsAngleSpecified()) {
    mPositions[0].mAngle = 0.0;
  }

  nsPresContext* presContext = PresContext();
  bool vertical = GetWritingMode().IsVertical();

  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(
      presContext->AppUnitsPerDevPixel());
  double factor = cssPxPerDevPx / mFontSizeScaleFactor;

  // Determine how much to compress or expand glyph positions due to
  // textLength="" and lengthAdjust="".
  double adjustment = 0.0;
  mLengthAdjustScaleFactor = 1.0f;
  if (adjustingTextLength) {
    nscoord frameLength =
        vertical ? PrincipalChildList().FirstChild()->GetRect().height
                 : PrincipalChildList().FirstChild()->GetRect().width;
    float actualTextLength = static_cast<float>(
        presContext->AppUnitsToGfxUnits(frameLength) * factor);

    switch (lengthAdjust) {
      case LENGTHADJUST_SPACINGANDGLYPHS:
        // Scale the glyphs and their positions.
        if (actualTextLength > 0) {
          mLengthAdjustScaleFactor = expectedTextLength / actualTextLength;
        }
        break;

      default:
        MOZ_ASSERT(lengthAdjust == LENGTHADJUST_SPACING);
        // Just add space between each glyph.
        int32_t adjustableSpaces = 0;
        for (uint32_t i = 1; i < mPositions.Length(); i++) {
          if (!mPositions[i].mUnaddressable) {
            adjustableSpaces++;
          }
        }
        if (adjustableSpaces) {
          adjustment =
              (expectedTextLength - actualTextLength) / adjustableSpaces;
        }
        break;
    }
  }

  // Fill in any unspecified character positions based on the positions recorded
  // in charPositions, and also add in the dx/dy values.
  if (!deltas.IsEmpty()) {
    mPositions[0].mPosition += deltas[0];
  }

  gfxFloat xLengthAdjustFactor = vertical ? 1.0 : mLengthAdjustScaleFactor;
  gfxFloat yLengthAdjustFactor = vertical ? mLengthAdjustScaleFactor : 1.0;
  for (uint32_t i = 1; i < mPositions.Length(); i++) {
    // Fill in unspecified x position.
    if (!mPositions[i].IsXSpecified()) {
      nscoord d = charPositions[i].x - charPositions[i - 1].x;
      mPositions[i].mPosition.x =
          mPositions[i - 1].mPosition.x +
          presContext->AppUnitsToGfxUnits(d) * factor * xLengthAdjustFactor;
      if (!vertical && !mPositions[i].mUnaddressable) {
        mPositions[i].mPosition.x += adjustment;
      }
    }
    // Fill in unspecified y position.
    if (!mPositions[i].IsYSpecified()) {
      nscoord d = charPositions[i].y - charPositions[i - 1].y;
      mPositions[i].mPosition.y =
          mPositions[i - 1].mPosition.y +
          presContext->AppUnitsToGfxUnits(d) * factor * yLengthAdjustFactor;
      if (vertical && !mPositions[i].mUnaddressable) {
        mPositions[i].mPosition.y += adjustment;
      }
    }
    // Add in dx/dy.
    if (i < deltas.Length()) {
      mPositions[i].mPosition += deltas[i];
    }
    // Fill in unspecified rotation values.
    if (!mPositions[i].IsAngleSpecified()) {
      mPositions[i].mAngle = 0.0f;
    }
  }

  MOZ_ASSERT(mPositions.Length() == charPositions.Length());

  AdjustChunksForLineBreaks();
  AdjustPositionsForClusters();
  DoAnchoring();
  DoTextPathLayout();
}

bool SVGTextFrame::ShouldRenderAsPath(nsTextFrame* aFrame,
                                      bool& aShouldPaintSVGGlyphs) {
  // Rendering to a clip path.
  if (HasAnyStateBits(NS_STATE_SVG_CLIPPATH_CHILD)) {
    aShouldPaintSVGGlyphs = false;
    return true;
  }

  aShouldPaintSVGGlyphs = true;

  const nsStyleSVG* style = aFrame->StyleSVG();

  // Fill is a non-solid paint, has a non-default fill-rule or has
  // non-1 opacity.
  if (!(style->mFill.kind.IsNone() ||
        (style->mFill.kind.IsColor() && style->mFillOpacity.IsOpacity() &&
         style->mFillOpacity.AsOpacity() == 1))) {
    return true;
  }

  // Text has a stroke.
  if (style->HasStroke()) {
    if (style->mStrokeWidth.IsContextValue()) {
      return true;
    }
    if (SVGContentUtils::CoordToFloat(
            static_cast<SVGElement*>(GetContent()),
            style->mStrokeWidth.AsLengthPercentage()) > 0) {
      return true;
    }
  }

  return false;
}

void SVGTextFrame::ScheduleReflowSVG() {
  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    ScheduleReflowSVGNonDisplayText(
        IntrinsicDirty::FrameAncestorsAndDescendants);
  } else {
    SVGUtils::ScheduleReflowSVG(this);
  }
}

void SVGTextFrame::NotifyGlyphMetricsChange(bool aUpdateTextCorrespondence) {
  if (aUpdateTextCorrespondence) {
    AddStateBits(NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY);
  }
  AddStateBits(NS_STATE_SVG_POSITIONING_DIRTY);
  nsLayoutUtils::PostRestyleEvent(mContent->AsElement(), RestyleHint{0},
                                  nsChangeHint_InvalidateRenderingObservers);
  ScheduleReflowSVG();
}

void SVGTextFrame::UpdateGlyphPositioning() {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (!kid) {
    return;
  }

  if (HasAnyStateBits(NS_STATE_SVG_TEXT_IN_REFLOW)) {
    // The anonymous block's frames are being modified (see
    // MaybeResolveBidiForAnonymousBlockChild), so positions can't be computed
    // now. Leave no stale positions behind for callers to iterate over.
    mPositions.Clear();
    return;
  }

  if (HasAnyStateBits(NS_STATE_SVG_POSITIONING_DIRTY)) {
    DoGlyphPositioning();
  }
}

void SVGTextFrame::MaybeResolveBidiForAnonymousBlockChild() {
  nsIFrame* kid = PrincipalChildList().FirstChild();

  if (kid && kid->HasAnyStateBits(NS_BLOCK_NEEDS_BIDI_RESOLUTION) &&
      PresContext()->BidiEnabled()) {
    MOZ_ASSERT(static_cast<nsBlockFrame*>(do_QueryFrame(kid)),
               "Expect anonymous child to be an nsBlockFrame");
    // Inserting continuations can synchronously notify rendering observers,
    // which may re-enter this frame (e.g. via a bbox query) or reflow its
    // block and invalidate the line state that bidi resolution is holding.
    const bool wasInReflow = HasAnyStateBits(NS_STATE_SVG_TEXT_IN_REFLOW);
    AddStateBits(NS_STATE_SVG_TEXT_IN_REFLOW);
    nsBidiPresUtils::Resolve(static_cast<nsBlockFrame*>(kid));
    if (!wasInReflow) {
      RemoveStateBits(NS_STATE_SVG_TEXT_IN_REFLOW);
    }
  }
}

void SVGTextFrame::MaybeReflowAnonymousBlockChild() {
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (!kid) {
    return;
  }

  MOZ_ASSERT(!kid->HasAnyStateBits(NS_FRAME_IN_REFLOW),
             "should not be in reflow when about to reflow again");

  if (IsSubtreeDirty()) {
    if (HasAnyStateBits(NS_FRAME_IS_DIRTY)) {
      // If we require a full reflow, ensure our kid is marked fully dirty.
      // (Note that our anonymous nsBlockFrame is not an ISVGDisplayableFrame,
      // so even when we are called via our ReflowSVG this will not be done for
      // us by SVGDisplayContainerFrame::ReflowSVG.)
      kid->MarkSubtreeDirty();
    }

    // The RecordCorrespondence and DoReflow calls can result in new text frames
    // being created (due to bidi resolution or reflow).  We set this bit to
    // guard against unnecessarily calling back in to
    // ScheduleReflowSVGNonDisplayText from nsIFrame::DidSetComputedStyle on
    // those new text frames.
    AddStateBits(NS_STATE_SVG_TEXT_IN_REFLOW);

    TextNodeCorrespondenceRecorder::RecordCorrespondence(this);

    MOZ_ASSERT(SVGUtils::AnyOuterSVGIsCallingReflowSVG(this),
               "should be under ReflowSVG");
    nsPresContext::InterruptPreventer noInterrupts(PresContext());
    DoReflow();

    RemoveStateBits(NS_STATE_SVG_TEXT_IN_REFLOW);
  }
}

void SVGTextFrame::DoReflow() {
  MOZ_ASSERT(HasAnyStateBits(NS_STATE_SVG_TEXT_IN_REFLOW));

  // Since we are going to reflow the anonymous block frame, we will
  // need to update mPositions.
  // We also mark our text correspondence as dirty since we can end up needing
  // reflow in ways that do not set NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY.
  // (We'd then fail the "expected a TextNodeCorrespondenceProperty" assertion
  // when UpdateGlyphPositioning() is called after we return.)
  AddStateBits(NS_STATE_SVG_TEXT_CORRESPONDENCE_DIRTY |
               NS_STATE_SVG_POSITIONING_DIRTY);

  if (HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    // Normally, these dirty flags would be cleared in ReflowSVG(), but that
    // doesn't get called for non-display frames. We don't want to reflow our
    // descendants every time SVGTextFrame::PaintSVG makes sure that we have
    // valid positions by calling UpdateGlyphPositioning(), so we need to clear
    // these dirty bits. Note that this also breaks an invalidation loop where
    // our descendants invalidate as they reflow, which invalidates rendering
    // observers, which reschedules the frame that is currently painting by
    // referencing us to paint again. See bug 839958 comment 7. Hopefully we
    // will break that loop more convincingly at some point.
    RemoveStateBits(NS_FRAME_IS_DIRTY | NS_FRAME_HAS_DIRTY_CHILDREN);
  }

  nsPresContext* presContext = PresContext();
  nsIFrame* kid = PrincipalChildList().FirstChild();
  if (!kid) {
    return;
  }

  UniquePtr<gfxContext> renderingContext =
      presContext->PresShell()->CreateReferenceRenderingContext();

  if (UpdateFontSizeScaleFactor()) {
    // If the font size scale factor changed, we need the block to report
    // an updated preferred width.
    kid->MarkIntrinsicISizesDirty();
  }

  nscoord inlineSize = kid->GetPrefISize(renderingContext.get());
  WritingMode wm = kid->GetWritingMode();
  ReflowInput reflowInput(presContext, kid, renderingContext.get(),
                          LogicalSize(wm, inlineSize, NS_UNCONSTRAINEDSIZE));
  ReflowOutput desiredSize(reflowInput);
  nsReflowStatus status;

  NS_ASSERTION(
      reflowInput.ComputedPhysicalBorderPadding() == nsMargin(0, 0, 0, 0) &&
          reflowInput.ComputedPhysicalMargin() == nsMargin(0, 0, 0, 0),
      "style system should ensure that :-moz-svg-text "
      "does not get styled");

  kid->Reflow(presContext, desiredSize, reflowInput, status);
  kid->DidReflow(presContext, &reflowInput);
  kid->SetSize(wm, desiredSize.Size(wm));
}

// Usable font size range in devpixels / user-units
#define CLAMP_MIN_SIZE 8.0
#define CLAMP_MAX_SIZE 200.0
#define PRECISE_SIZE 200.0

bool SVGTextFrame::UpdateFontSizeScaleFactor() {
  double oldFontSizeScaleFactor = mFontSizeScaleFactor;

  nsPresContext* presContext = PresContext();

  bool geometricPrecision = false;
  CSSCoord min = std::numeric_limits<float>::max();
  CSSCoord max = std::numeric_limits<float>::min();
  bool anyText = false;

  // Find the minimum and maximum font sizes used over all the
  // nsTextFrames.
  TextFrameIterator it(this);
  nsTextFrame* f = it.Current();
  while (f) {
    if (!geometricPrecision) {
      // Unfortunately we can't treat text-rendering:geometricPrecision
      // separately for each text frame.
      geometricPrecision = f->StyleText()->mTextRendering ==
                           StyleTextRendering::Geometricprecision;
    }
    const auto& fontSize = f->StyleFont()->mFont.size;
    if (!fontSize.IsZero()) {
      min = std::min(min, fontSize.ToCSSPixels());
      max = std::max(max, fontSize.ToCSSPixels());
      anyText = true;
    }
    f = it.Next();
  }

  if (!anyText) {
    // No text, so no need for scaling.
    mFontSizeScaleFactor = 1.0;
    return mFontSizeScaleFactor != oldFontSizeScaleFactor;
  }

  if (geometricPrecision) {
    // We want to ensure minSize is scaled to PRECISE_SIZE.
    mFontSizeScaleFactor = PRECISE_SIZE / min;
    return mFontSizeScaleFactor != oldFontSizeScaleFactor;
  }

  // When we are non-display, we could be painted in different coordinate
  // spaces, and we don't want to have to reflow for each of these.  We
  // just assume that the context scale is 1.0 for them all, so we don't
  // get stuck with a font size scale factor based on whichever referencing
  // frame happens to reflow first.
  double contextScale = 1.0;
  if (!HasAnyStateBits(NS_FRAME_IS_NONDISPLAY)) {
    gfxMatrix m(GetCanvasTM());
    if (!m.IsSingular()) {
      contextScale = GetContextScale(m);
      if (!std::isfinite(contextScale)) {
        contextScale = 1.0f;
      }
    }
  }
  mLastContextScale = contextScale;

  // But we want to ignore any scaling required due to HiDPI displays, since
  // regular CSS text frames will still create text runs using the font size
  // in CSS pixels, and we want SVG text to have the same rendering as HTML
  // text for regular font sizes.
  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(
      presContext->AppUnitsPerDevPixel());
  contextScale *= cssPxPerDevPx;

  double minTextRunSize = min * contextScale;
  double maxTextRunSize = max * contextScale;

  if (minTextRunSize >= CLAMP_MIN_SIZE && maxTextRunSize <= CLAMP_MAX_SIZE) {
    // We are already in the ideal font size range for all text frames,
    // so we only have to take into account the contextScale.
    mFontSizeScaleFactor = contextScale;
  } else if (max / min > CLAMP_MAX_SIZE / CLAMP_MIN_SIZE) {
    // We can't scale the font sizes so that all of the text frames lie
    // within our ideal font size range.
    // Heuristically, if the maxTextRunSize is within the CLAMP_MAX_SIZE
    // as a reasonable value, it's likely to be the user's intent to
    // get a valid font for the maxTextRunSize one, we should honor it.
    // The same for minTextRunSize.
    if (maxTextRunSize <= CLAMP_MAX_SIZE) {
      mFontSizeScaleFactor = CLAMP_MAX_SIZE / max;
    } else if (minTextRunSize >= CLAMP_MIN_SIZE) {
      mFontSizeScaleFactor = CLAMP_MIN_SIZE / min;
    } else {
      // So maxTextRunSize is too big, minTextRunSize is too small,
      // we can't really do anything for this case, just leave it as is.
      mFontSizeScaleFactor = contextScale;
    }
  } else if (minTextRunSize < CLAMP_MIN_SIZE) {
    mFontSizeScaleFactor = CLAMP_MIN_SIZE / min;
  } else {
    mFontSizeScaleFactor = CLAMP_MAX_SIZE / max;
  }

  return mFontSizeScaleFactor != oldFontSizeScaleFactor;
}

double SVGTextFrame::GetFontSizeScaleFactor() const {
  return mFontSizeScaleFactor;
}

/**
 * Take aPoint, which is in the <text> element's user space, and convert
 * it to the appropriate frame user space of aChildFrame according to
 * which rendered run the point hits.
 */
Point SVGTextFrame::TransformFramePointToTextChild(
    const Point& aPoint, const nsIFrame* aChildFrame) {
  NS_ASSERTION(aChildFrame && nsLayoutUtils::GetClosestFrameOfType(
                                  aChildFrame->GetParent(),
                                  LayoutFrameType::SVGText) == this,
               "aChildFrame must be a descendant of this frame");

  UpdateGlyphPositioning();

  nsPresContext* presContext = PresContext();

  // Add in the mRect offset to aPoint, as that will have been taken into
  // account when transforming the point from the ancestor frame down
  // to this one.
  float cssPxPerDevPx = nsPresContext::AppUnitsToFloatCSSPixels(
      presContext->AppUnitsPerDevPixel());
  float factor = AppUnitsPerCSSPixel();
  Point framePosition(NSAppUnitsToFloatPixels(mRect.x, factor),
                      NSAppUnitsToFloatPixels(mRect.y, factor));
  Point pointInUserSpace = aPoint * cssPxPerDevPx + framePosition;

  // Find the closest rendered run for the text frames beneath aChildFrame.
  TextRenderedRunIterator it(this, TextRenderedRunIterator::eAllFrames,
                             aChildFrame);
  TextRenderedRun hit;
  gfxPoint pointInRun;
  nscoord dx = nscoord_MAX;
  nscoord dy = nscoord_MAX;
  for (TextRenderedRun run = it.Current(); run.mFrame; run = it.Next()) {
    uint32_t flags = TextRenderedRun::eIncludeFill |
                     TextRenderedRun::eIncludeStroke |
                     TextRenderedRun::eNoHorizontalOverflow;
    gfxRect runRect =
        run.GetRunUserSpaceRect(presContext, flags).ToThebesRect();

    gfxMatrix m = run.GetTransformFromRunUserSpaceToUserSpace(presContext);
    if (!m.Invert()) {
      return aPoint;
    }
    gfxPoint pointInRunUserSpace =
        m.TransformPoint(ThebesPoint(pointInUserSpace));

    if (Inside(runRect, pointInRunUserSpace)) {
      // The point was inside the rendered run's rect, so we choose it.
      dx = 0;
      dy = 0;
      pointInRun = pointInRunUserSpace;
      hit = run;
    } else if (nsLayoutUtils::PointIsCloserToRect(pointInRunUserSpace, runRect,
                                                  dx, dy)) {
      // The point was closer to this rendered run's rect than any others
      // we've seen so far.
      pointInRun.x =
          clamped(pointInRunUserSpace.x.value, runRect.X(), runRect.XMost());
      pointInRun.y =
          clamped(pointInRunUserSpace.y.value, runRect.Y(), runRect.YMost());
      hit = run;
    }
  }

  if (!hit.mFrame) {
    // We didn't find any rendered runs for the frame.
    return aPoint;
  }

  // Return the point in user units relative to the nsTextFrame,
  // but taking into account mFontSizeScaleFactor.
  gfxMatrix m = hit.GetTransformFromRunUserSpaceToFrameUserSpace(presContext);
  m.PreScale(mFontSizeScaleFactor, mFontSizeScaleFactor);
  return ToPoint(m.TransformPoint(pointInRun) / cssPxPerDevPx);
}

/**
 * For each rendered run beneath aChildFrame, translate aRect from
 * aChildFrame to the run's text frame, transform it then into
 * the run's frame user space, intersect it with the run's
 * frame user space rect, then transform it up to user space.
 * The result is the union of all of these.
 */
gfxRect SVGTextFrame::TransformFrameRectFromTextChild(
    const nsRect& aRect, const nsIFrame* aChildFrame) {
  NS_ASSERTION(aChildFrame && nsLayoutUtils::GetClosestFrameOfType(
                                  aChildFrame->GetParent(),
                                  LayoutFrameType::SVGText) == this,
               "aChildFrame must be a descendant of this frame");

  UpdateGlyphPositioning();

  nsPresContext* presContext = PresContext();

  gfxRect result;
  TextRenderedRunIterator it(this, TextRenderedRunIterator::eAllFrames,
                             aChildFrame);
  for (TextRenderedRun run = it.Current(); run.mFrame; run = it.Next()) {
    // First, translate aRect from aChildFrame to this run's frame.
    nsRect rectInTextFrame = aRect + aChildFrame->GetOffsetTo(run.mFrame);

    // Scale it into frame user space.
    gfxRect rectInFrameUserSpace = AppUnitsToFloatCSSPixels(
        gfxRect(rectInTextFrame.x, rectInTextFrame.y, rectInTextFrame.width,
                rectInTextFrame.height),
        presContext);

    // Intersect it with the run.
    uint32_t flags =
        TextRenderedRun::eIncludeFill | TextRenderedRun::eIncludeStroke;

    if (rectInFrameUserSpace.IntersectRect(
            rectInFrameUserSpace,
            run.GetFrameUserSpaceRect(presContext, flags).ToThebesRect())) {
      // Transform it up to user space of the <text>
      gfxMatrix m = run.GetTransformFromRunUserSpaceToUserSpace(presContext);
      gfxRect rectInUserSpace = m.TransformRect(rectInFrameUserSpace);

      // Union it into the result.
      result.UnionRect(result, rectInUserSpace);
    }
  }

  // Subtract the mRect offset from the result, as our user space for
  // this frame is relative to the top-left of mRect.
  float factor = AppUnitsPerCSSPixel();
  gfxPoint framePosition(NSAppUnitsToFloatPixels(mRect.x, factor),
                         NSAppUnitsToFloatPixels(mRect.y, factor));

  return result - framePosition;
}

Rect SVGTextFrame::TransformFrameRectFromTextChild(
    const Rect& aRect, const nsIFrame* aChildFrame) {
  nscoord appUnitsPerDevPixel = PresContext()->AppUnitsPerDevPixel();
  nsRect r = LayoutDevicePixel::ToAppUnits(
      LayoutDeviceRect::FromUnknownRect(aRect), appUnitsPerDevPixel);
  gfxRect resultCssUnits = TransformFrameRectFromTextChild(r, aChildFrame);
  float devPixelPerCSSPixel =
      float(AppUnitsPerCSSPixel()) / appUnitsPerDevPixel;
  resultCssUnits.Scale(devPixelPerCSSPixel);
  return ToRect(resultCssUnits);
}

Point SVGTextFrame::TransformFramePointFromTextChild(
    const Point& aPoint, const nsIFrame* aChildFrame) {
  return TransformFrameRectFromTextChild(Rect(aPoint, Size(1, 1)), aChildFrame)
      .TopLeft();
}

void SVGTextFrame::AppendDirectlyOwnedAnonBoxes(
    nsTArray<OwnedAnonBox>& aResult) {
  MOZ_ASSERT(PrincipalChildList().FirstChild(), "Must have our anon box");
  aResult.AppendElement(OwnedAnonBox(PrincipalChildList().FirstChild()));
}

}  // namespace mozilla
