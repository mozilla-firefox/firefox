/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* utility functions for drawing borders and backgrounds */

#include "nsCSSRendering.h"

#include <algorithm>
#include <ctime>
#include <limits>

#include "BorderConsts.h"
#include "ImageContainer.h"
#include "ImageOps.h"
#include "ScaledFontBase.h"
#include "TextDrawTarget.h"
#include "gfx2DGlue.h"
#include "gfxContext.h"
#include "gfxDrawable.h"
#include "gfxFont.h"
#include "gfxGradientCache.h"
#include "gfxUtils.h"
#include "imgIContainer.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/SVGImageContext.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/css/ImageLoader.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Helpers.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/PathHelpers.h"
#include "nsBlockFrame.h"
#include "nsCSSColorUtils.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSProps.h"
#include "nsCSSRenderingBorders.h"
#include "nsCanvasFrame.h"
#include "nsContentUtils.h"
#include "nsFrameManager.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsITheme.h"
#include "nsInlineFrame.h"
#include "nsLayoutUtils.h"
#include "nsPageSequenceFrame.h"
#include "nsPoint.h"
#include "nsPresContext.h"
#include "nsRect.h"
#include "nsRubyTextContainerFrame.h"
#include "nsStyleConsts.h"
#include "nsStyleStructInlines.h"
#include "skia/include/core/SkTextBlob.h"

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::gfx;
using namespace mozilla::image;
using mozilla::CSSSizeOrRatio;
using mozilla::dom::Document;

// To avoid storing this data on nsInlineFrame (bloat) and to avoid
// recalculating this for each frame in a continuation (perf), hold
// a cache of various coordinate information that we need in order
// to paint inline backgrounds.
struct InlineBackgroundData {
  InlineBackgroundData()
      : mFrame(nullptr),
        mLineContainer(nullptr),
        mContinuationPoint(0),
        mUnbrokenMeasure(0),
        mLineContinuationPoint(0),
        mPIStartBorderData{},
        mBidiEnabled(false),
        mVertical(false) {}

  ~InlineBackgroundData() = default;

  void Reset() {
    mBoundingBox.SetRect(0, 0, 0, 0);
    mContinuationPoint = mLineContinuationPoint = mUnbrokenMeasure = 0;
    mFrame = mLineContainer = nullptr;
    mPIStartBorderData.Reset();
  }

  /**
   * Return a continuous rect for (an inline) aFrame relative to the
   * continuation that draws the left-most part of the background.
   * This is used when painting backgrounds.
   */
  nsRect GetContinuousRect(nsIFrame* aFrame) {
    MOZ_ASSERT(aFrame->IsInlineFrameOrSubclass());

    SetFrame(aFrame);

    nscoord pos;  // an x coordinate if writing-mode is horizontal;
                  // y coordinate if vertical
    if (mBidiEnabled) {
      pos = mLineContinuationPoint;

      // Scan continuations on the same line as aFrame and accumulate the widths
      // of frames that are to the left (if this is an LTR block) or right
      // (if it's RTL) of the current one.
      bool isRtlBlock = (mLineContainer->StyleVisibility()->mDirection ==
                         StyleDirection::Rtl);
      nscoord curOffset = mVertical ? aFrame->GetOffsetTo(mLineContainer).y
                                    : aFrame->GetOffsetTo(mLineContainer).x;

      // If the continuation is fluid we know inlineFrame is not on the same
      // line. If it's not fluid, we need to test further to be sure.
      nsIFrame* inlineFrame = aFrame->GetPrevContinuation();
      while (inlineFrame && !inlineFrame->GetNextInFlow() &&
             AreOnSameLine(aFrame, inlineFrame)) {
        nscoord frameOffset = mVertical
                                  ? inlineFrame->GetOffsetTo(mLineContainer).y
                                  : inlineFrame->GetOffsetTo(mLineContainer).x;
        if (isRtlBlock == (frameOffset >= curOffset)) {
          pos += mVertical ? inlineFrame->GetSize().height
                           : inlineFrame->GetSize().width;
        }
        inlineFrame = inlineFrame->GetPrevContinuation();
      }

      inlineFrame = aFrame->GetNextContinuation();
      while (inlineFrame && !inlineFrame->GetPrevInFlow() &&
             AreOnSameLine(aFrame, inlineFrame)) {
        nscoord frameOffset = mVertical
                                  ? inlineFrame->GetOffsetTo(mLineContainer).y
                                  : inlineFrame->GetOffsetTo(mLineContainer).x;
        if (isRtlBlock == (frameOffset >= curOffset)) {
          pos += mVertical ? inlineFrame->GetSize().height
                           : inlineFrame->GetSize().width;
        }
        inlineFrame = inlineFrame->GetNextContinuation();
      }
      if (isRtlBlock) {
        // aFrame itself is also to the right of its left edge, so add its
        // width.
        pos += mVertical ? aFrame->GetSize().height : aFrame->GetSize().width;
        // pos is now the distance from the left [top] edge of aFrame to the
        // right [bottom] edge of the unbroken content. Change it to indicate
        // the distance from the left [top] edge of the unbroken content to the
        // left [top] edge of aFrame.
        pos = mUnbrokenMeasure - pos;
      }
    } else {
      pos = mContinuationPoint;
    }

    // Assume background-origin: border and return a rect with offsets
    // relative to (0,0).  If we have a different background-origin,
    // then our rect should be deflated appropriately by our caller.
    return mVertical
               ? nsRect(0, -pos, mFrame->GetSize().width, mUnbrokenMeasure)
               : nsRect(-pos, 0, mUnbrokenMeasure, mFrame->GetSize().height);
  }

  /**
   * Return a continuous rect for (an inline) aFrame relative to the
   * continuation that should draw the left[top]-border.  This is used when
   * painting borders and clipping backgrounds.  This may NOT be the same
   * continuous rect as for drawing backgrounds; the continuation with the
   * left[top]-border might be somewhere in the middle of that rect (e.g. BIDI),
   * in those cases we need the reverse background order starting at the
   * left[top]-border continuation.
   */
  nsRect GetBorderContinuousRect(nsIFrame* aFrame, nsRect aBorderArea) {
    // Calling GetContinuousRect(aFrame) here may lead to Reset/Init which
    // resets our mPIStartBorderData so we save it ...
    PhysicalInlineStartBorderData saved(mPIStartBorderData);
    nsRect joinedBorderArea = GetContinuousRect(aFrame);
    if (!saved.mIsValid || saved.mFrame != mPIStartBorderData.mFrame) {
      if (aFrame == mPIStartBorderData.mFrame) {
        if (mVertical) {
          mPIStartBorderData.SetCoord(joinedBorderArea.y);
        } else {
          mPIStartBorderData.SetCoord(joinedBorderArea.x);
        }
      } else if (mPIStartBorderData.mFrame) {
        // Copy data to a temporary object so that computing the
        // continous rect here doesn't clobber our normal state.
        InlineBackgroundData temp = *this;
        if (mVertical) {
          mPIStartBorderData.SetCoord(
              temp.GetContinuousRect(mPIStartBorderData.mFrame).y);
        } else {
          mPIStartBorderData.SetCoord(
              temp.GetContinuousRect(mPIStartBorderData.mFrame).x);
        }
      }
    } else {
      // ... and restore it when possible.
      mPIStartBorderData.SetCoord(saved.mCoord);
    }
    if (mVertical) {
      if (joinedBorderArea.y > mPIStartBorderData.mCoord) {
        joinedBorderArea.y =
            -(mUnbrokenMeasure + joinedBorderArea.y - aBorderArea.height);
      } else {
        joinedBorderArea.y -= mPIStartBorderData.mCoord;
      }
    } else {
      if (joinedBorderArea.x > mPIStartBorderData.mCoord) {
        joinedBorderArea.x =
            -(mUnbrokenMeasure + joinedBorderArea.x - aBorderArea.width);
      } else {
        joinedBorderArea.x -= mPIStartBorderData.mCoord;
      }
    }
    return joinedBorderArea;
  }

  nsRect GetBoundingRect(nsIFrame* aFrame) {
    SetFrame(aFrame);

    // Move the offsets relative to (0,0) which puts the bounding box into
    // our coordinate system rather than our parent's.  We do this by
    // moving it the back distance from us to the bounding box.
    // This also assumes background-origin: border, so our caller will
    // need to deflate us if needed.
    nsRect boundingBox(mBoundingBox);
    nsPoint point = mFrame->GetPosition();
    boundingBox.MoveBy(-point.x, -point.y);

    return boundingBox;
  }

 protected:
  // This is a coordinate on the inline axis, but is not a true logical inline-
  // coord because it is always measured from left to right (if horizontal) or
  // from top to bottom (if vertical), ignoring any bidi RTL directionality.
  // We'll call this "physical inline start", or PIStart for short.
  struct PhysicalInlineStartBorderData {
    nsIFrame* mFrame;  // the continuation that may have a left-border
    nscoord mCoord;    // cached GetContinuousRect(mFrame).x or .y
    bool mIsValid;     // true if mCoord is valid
    void Reset() {
      mFrame = nullptr;
      mIsValid = false;
    }
    void SetCoord(nscoord aCoord) {
      mCoord = aCoord;
      mIsValid = true;
    }
  };

  nsIFrame* mFrame;
  nsIFrame* mLineContainer;
  nsRect mBoundingBox;
  nscoord mContinuationPoint;
  nscoord mUnbrokenMeasure;
  nscoord mLineContinuationPoint;
  PhysicalInlineStartBorderData mPIStartBorderData;
  bool mBidiEnabled;
  bool mVertical;

  void SetFrame(nsIFrame* aFrame) {
    MOZ_ASSERT(aFrame, "Need a frame");

    if (aFrame == mFrame) {
      return;
    }

    nsIFrame* prevContinuation = GetPrevContinuation(aFrame);

    if (!prevContinuation || mFrame != prevContinuation) {
      // Ok, we've got the wrong frame.  We have to start from scratch.
      Reset();
      Init(aFrame);
      return;
    }

    // Get our last frame's size and add its width to our continuation
    // point before we cache the new frame.
    mContinuationPoint +=
        mVertical ? mFrame->GetSize().height : mFrame->GetSize().width;

    // If this a new line, update mLineContinuationPoint.
    if (mBidiEnabled &&
        (aFrame->GetPrevInFlow() || !AreOnSameLine(mFrame, aFrame))) {
      mLineContinuationPoint = mContinuationPoint;
    }

    mFrame = aFrame;
  }

  nsIFrame* GetPrevContinuation(nsIFrame* aFrame) {
    nsIFrame* prevCont = aFrame->GetPrevContinuation();
    if (!prevCont && aFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
      nsIFrame* block = aFrame->GetProperty(nsIFrame::IBSplitPrevSibling());
      if (block) {
        // The {ib} properties are only stored on first continuations
        NS_ASSERTION(!block->GetPrevContinuation(),
                     "Incorrect value for IBSplitPrevSibling");
        prevCont = block->GetProperty(nsIFrame::IBSplitPrevSibling());
        NS_ASSERTION(prevCont, "How did that happen?");
      }
    }
    return prevCont;
  }

  nsIFrame* GetNextContinuation(nsIFrame* aFrame) {
    nsIFrame* nextCont = aFrame->GetNextContinuation();
    if (!nextCont && aFrame->HasAnyStateBits(NS_FRAME_PART_OF_IBSPLIT)) {
      // The {ib} properties are only stored on first continuations
      aFrame = aFrame->FirstContinuation();
      nsIFrame* block = aFrame->GetProperty(nsIFrame::IBSplitSibling());
      if (block) {
        nextCont = block->GetProperty(nsIFrame::IBSplitSibling());
        NS_ASSERTION(nextCont, "How did that happen?");
      }
    }
    return nextCont;
  }

  void Init(nsIFrame* aFrame) {
    mPIStartBorderData.Reset();
    mBidiEnabled = aFrame->PresContext()->BidiEnabled();
    if (mBidiEnabled) {
      // Find the line container frame
      mLineContainer = aFrame;
      while (mLineContainer && mLineContainer->IsLineParticipant()) {
        mLineContainer = mLineContainer->GetParent();
      }

      MOZ_ASSERT(mLineContainer, "Cannot find line containing frame.");
      MOZ_ASSERT(mLineContainer != aFrame,
                 "line container frame "
                 "should be an ancestor of the target frame.");
    }

    mVertical = aFrame->GetWritingMode().IsVertical();

    // Start with the previous flow frame as our continuation point
    // is the total of the widths of the previous frames.
    nsIFrame* inlineFrame = GetPrevContinuation(aFrame);
    bool changedLines = false;
    while (inlineFrame) {
      if (!mPIStartBorderData.mFrame &&
          !(mVertical ? inlineFrame->GetSkipSides().Top()
                      : inlineFrame->GetSkipSides().Left())) {
        mPIStartBorderData.mFrame = inlineFrame;
      }
      nsRect rect = inlineFrame->GetRect();
      mContinuationPoint += mVertical ? rect.height : rect.width;
      if (mBidiEnabled &&
          (changedLines || !AreOnSameLine(aFrame, inlineFrame))) {
        mLineContinuationPoint += mVertical ? rect.height : rect.width;
        changedLines = true;
      }
      mUnbrokenMeasure += mVertical ? rect.height : rect.width;
      mBoundingBox.UnionRect(mBoundingBox, rect);
      inlineFrame = GetPrevContinuation(inlineFrame);
    }

    // Next add this frame and subsequent frames to the bounding box and
    // unbroken width.
    inlineFrame = aFrame;
    while (inlineFrame) {
      if (!mPIStartBorderData.mFrame &&
          !(mVertical ? inlineFrame->GetSkipSides().Top()
                      : inlineFrame->GetSkipSides().Left())) {
        mPIStartBorderData.mFrame = inlineFrame;
      }
      nsRect rect = inlineFrame->GetRect();
      mUnbrokenMeasure += mVertical ? rect.height : rect.width;
      mBoundingBox.UnionRect(mBoundingBox, rect);
      inlineFrame = GetNextContinuation(inlineFrame);
    }

    mFrame = aFrame;
  }

  bool AreOnSameLine(nsIFrame* aFrame1, nsIFrame* aFrame2) {
    if (nsBlockFrame* blockFrame = do_QueryFrame(mLineContainer)) {
      bool isValid1, isValid2;
      nsBlockInFlowLineIterator it1(blockFrame, aFrame1, &isValid1);
      nsBlockInFlowLineIterator it2(blockFrame, aFrame2, &isValid2);
      return isValid1 && isValid2 &&
             // Make sure aFrame1 and aFrame2 are in the same continuation of
             // blockFrame.
             it1.GetContainer() == it2.GetContainer() &&
             // And on the same line in it
             it1.GetLine().get() == it2.GetLine().get();
    }
    if (nsRubyTextContainerFrame* rtcFrame = do_QueryFrame(mLineContainer)) {
      nsBlockFrame* block = nsLayoutUtils::FindNearestBlockAncestor(rtcFrame);
      // Ruby text container can only hold one line of text, so if they
      // are in the same continuation, they are in the same line. Since
      // ruby text containers are bidi isolate, they are never split for
      // bidi reordering, which means being in different continuation
      // indicates being in different lines.
      for (nsIFrame* frame = rtcFrame->FirstContinuation(); frame;
           frame = frame->GetNextContinuation()) {
        bool isDescendant1 =
            nsLayoutUtils::IsProperAncestorFrame(frame, aFrame1, block);
        bool isDescendant2 =
            nsLayoutUtils::IsProperAncestorFrame(frame, aFrame2, block);
        if (isDescendant1 && isDescendant2) {
          return true;
        }
        if (isDescendant1 || isDescendant2) {
          return false;
        }
      }
      MOZ_ASSERT_UNREACHABLE("None of the frames is a descendant of this rtc?");
    }
    MOZ_ASSERT_UNREACHABLE("Do we have any other type of line container?");
    return false;
  }
};

static StaticAutoPtr<InlineBackgroundData> gInlineBGData;

// Initialize any static variables used by nsCSSRendering.
void nsCSSRendering::Init() {
  NS_ASSERTION(!gInlineBGData, "Init called twice");
  gInlineBGData = new InlineBackgroundData();
}

// Clean up any global variables used by nsCSSRendering.
void nsCSSRendering::Shutdown() { gInlineBGData = nullptr; }

/**
 * Make a bevel color
 */
static nscolor MakeBevelColor(mozilla::Side whichSide, StyleBorderStyle style,
                              nscolor aBorderColor) {
  nscolor colors[2];
  nscolor theColor;

  // Given a background color and a border color
  // calculate the color used for the shading
  NS_GetSpecial3DColors(colors, aBorderColor);

  if ((style == StyleBorderStyle::Outset) ||
      (style == StyleBorderStyle::Ridge)) {
    // Flip colors for these two border styles
    switch (whichSide) {
      case eSideBottom:
        whichSide = eSideTop;
        break;
      case eSideRight:
        whichSide = eSideLeft;
        break;
      case eSideTop:
        whichSide = eSideBottom;
        break;
      case eSideLeft:
        whichSide = eSideRight;
        break;
    }
  }

  switch (whichSide) {
    case eSideBottom:
      theColor = colors[1];
      break;
    case eSideRight:
      theColor = colors[1];
      break;
    case eSideTop:
      theColor = colors[0];
      break;
    case eSideLeft:
    default:
      theColor = colors[0];
      break;
  }
  return theColor;
}

static bool GetRadii(nsIFrame* aForFrame, const nsStyleBorder& aBorder,
                     const nsRect& aOrigBorderArea, const nsRect& aBorderArea,
                     nsRectCornerRadii& aRadii) {
  bool haveRoundedCorners;
  nsSize sz = aBorderArea.Size();
  nsSize frameSize = aForFrame->GetSize();
  if (&aBorder == aForFrame->StyleBorder() &&
      frameSize == aOrigBorderArea.Size()) {
    haveRoundedCorners = aForFrame->GetBorderRadii(sz, sz, Sides(), aRadii);
  } else {
    haveRoundedCorners = nsIFrame::ComputeBorderRadii(
        aBorder.mBorderRadius, aBorder.mCornerShape, frameSize, sz, Sides(),
        aRadii);
  }

  return haveRoundedCorners;
}

static bool GetRadii(nsIFrame* aForFrame, const nsStyleBorder& aBorder,
                     const nsRect& aOrigBorderArea, const nsRect& aBorderArea,
                     RectCornerRadii* aBgRadii) {
  nsRectCornerRadii radii;
  bool haveRoundedCorners =
      GetRadii(aForFrame, aBorder, aOrigBorderArea, aBorderArea, radii);

  if (haveRoundedCorners) {
    auto d2a = aForFrame->PresContext()->AppUnitsPerDevPixel();
    nsCSSRendering::ComputePixelRadii(radii, d2a, aBgRadii);
  }
  return haveRoundedCorners;
}

static nsRect JoinBoxesForBlockAxisSlice(nsIFrame* aFrame,
                                         const nsRect& aBorderArea) {
  // Inflate the block-axis size as if our continuations were laid out
  // adjacent in that axis.  Note that we don't touch the inline size.
  const auto wm = aFrame->GetWritingMode();
  const nsSize dummyContainerSize;
  LogicalRect borderArea(wm, aBorderArea, dummyContainerSize);
  nscoord bSize = 0;
  nsIFrame* f = aFrame->GetNextContinuation();
  for (; f; f = f->GetNextContinuation()) {
    bSize += f->BSize(wm);
  }
  borderArea.BSize(wm) += bSize;
  bSize = 0;
  f = aFrame->GetPrevContinuation();
  for (; f; f = f->GetPrevContinuation()) {
    bSize += f->BSize(wm);
  }
  borderArea.BStart(wm) -= bSize;
  borderArea.BSize(wm) += bSize;
  return borderArea.GetPhysicalRect(wm, dummyContainerSize);
}

/**
 * Inflate aBorderArea which is relative to aFrame's origin to calculate
 * a hypothetical non-split frame area for all the continuations.
 * See "Joining Boxes for 'slice'" in
 * http://dev.w3.org/csswg/css-break/#break-decoration
 */
enum InlineBoxOrder { eForBorder, eForBackground };
static nsRect JoinBoxesForSlice(nsIFrame* aFrame, const nsRect& aBorderArea,
                                InlineBoxOrder aOrder) {
  if (aFrame->IsInlineFrameOrSubclass()) {
    return (aOrder == eForBorder
                ? gInlineBGData->GetBorderContinuousRect(aFrame, aBorderArea)
                : gInlineBGData->GetContinuousRect(aFrame)) +
           aBorderArea.TopLeft();
  }
  return JoinBoxesForBlockAxisSlice(aFrame, aBorderArea);
}

/* static */
bool nsCSSRendering::IsBoxDecorationSlice(const nsStyleBorder& aStyleBorder) {
  return aStyleBorder.mBoxDecorationBreak == StyleBoxDecorationBreak::Slice;
}

/* static */
nsRect nsCSSRendering::BoxDecorationRectForBorder(
    nsIFrame* aFrame, const nsRect& aBorderArea, Sides aSkipSides,
    const nsStyleBorder* aStyleBorder) {
  if (!aStyleBorder) {
    aStyleBorder = aFrame->StyleBorder();
  }
  // If aSkipSides.IsEmpty() then there are no continuations, or it's
  // a ::first-letter that wants all border sides on the first continuation.
  return IsBoxDecorationSlice(*aStyleBorder) && !aSkipSides.IsEmpty()
             ? ::JoinBoxesForSlice(aFrame, aBorderArea, eForBorder)
             : aBorderArea;
}

/* static */
nsRect nsCSSRendering::BoxDecorationRectForBackground(
    nsIFrame* aFrame, const nsRect& aBorderArea, Sides aSkipSides,
    const nsStyleBorder* aStyleBorder) {
  if (!aStyleBorder) {
    aStyleBorder = aFrame->StyleBorder();
  }
  // If aSkipSides.IsEmpty() then there are no continuations, or it's
  // a ::first-letter that wants all border sides on the first continuation.
  return IsBoxDecorationSlice(*aStyleBorder) && !aSkipSides.IsEmpty()
             ? ::JoinBoxesForSlice(aFrame, aBorderArea, eForBackground)
             : aBorderArea;
}

//----------------------------------------------------------------------
// Thebes Border Rendering Code Start

/*
 * Compute the float-pixel radii that should be used for drawing
 * this border/outline, given the various input bits.
 */
/* static */
void nsCSSRendering::ComputePixelRadii(const nsRectCornerRadii& aRadii,
                                       nscoord aAppUnitsPerPixel,
                                       RectCornerRadii* oBorderRadii) {
  for (const auto corner : mozilla::AllPhysicalCorners()) {
    (*oBorderRadii)[corner] =
        LayoutDeviceSize::FromAppUnits(aRadii[corner], aAppUnitsPerPixel)
            .ToUnknownSize();
    oBorderRadii->mShapeK[corner] = aRadii.mShapeK[corner];
  }
}

static Maybe<nsStyleBorder> GetBorderIfVisited(const ComputedStyle& aStyle) {
  Maybe<nsStyleBorder> result;
  // Don't check RelevantLinkVisited here, since we want to take the
  // same amount of time whether or not it's true.
  const ComputedStyle* styleIfVisited = aStyle.GetStyleIfVisited();
  if (MOZ_LIKELY(!styleIfVisited)) {
    return result;
  }

  result.emplace(*aStyle.StyleBorder());
  auto& newBorder = result.ref();
  for (const auto side : mozilla::AllPhysicalSides()) {
    nscolor color = aStyle.GetVisitedDependentColor(
        nsStyleBorder::BorderColorFieldFor(side));
    newBorder.BorderColorFor(side) = StyleColor::FromColor(color);
  }

  return result;
}

ImgDrawResult nsCSSRendering::PaintBorder(
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    nsIFrame* aForFrame, const nsRect& aDirtyRect, const nsRect& aBorderArea,
    ComputedStyle* aStyle, PaintBorderFlags aFlags, Sides aSkipSides) {
  AUTO_PROFILER_LABEL("nsCSSRendering::PaintBorder", GRAPHICS);

  Maybe<nsStyleBorder> visitedBorder = GetBorderIfVisited(*aStyle);
  return PaintBorderWithStyleBorder(
      aPresContext, aRenderingContext, aForFrame, aDirtyRect, aBorderArea,
      visitedBorder.refOr(*aStyle->StyleBorder()), aStyle, aFlags, aSkipSides);
}

Maybe<nsCSSBorderRenderer> nsCSSRendering::CreateBorderRenderer(
    nsPresContext* aPresContext, DrawTarget* aDrawTarget, nsIFrame* aForFrame,
    const nsRect& aDirtyRect, const nsRect& aBorderArea, ComputedStyle* aStyle,
    bool* aOutBorderIsEmpty, Sides aSkipSides) {
  Maybe<nsStyleBorder> visitedBorder = GetBorderIfVisited(*aStyle);
  return CreateBorderRendererWithStyleBorder(
      aPresContext, aDrawTarget, aForFrame, aDirtyRect, aBorderArea,
      visitedBorder.refOr(*aStyle->StyleBorder()), aStyle, aOutBorderIsEmpty,
      aSkipSides);
}

ImgDrawResult nsCSSRendering::CreateWebRenderCommandsForBorder(
    nsDisplayItem* aItem, nsIFrame* aForFrame, const nsRect& aBorderArea,
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder) {
  const auto* style = aForFrame->Style();
  Maybe<nsStyleBorder> visitedBorder = GetBorderIfVisited(*style);
  return nsCSSRendering::CreateWebRenderCommandsForBorderWithStyleBorder(
      aItem, aForFrame, aBorderArea, aBuilder, aResources, aSc, aManager,
      aDisplayListBuilder, visitedBorder.refOr(*style->StyleBorder()));
}

void nsCSSRendering::CreateWebRenderCommandsForNullBorder(
    nsDisplayItem* aItem, nsIFrame* aForFrame, const nsRect& aBorderArea,
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    const nsStyleBorder& aStyleBorder) {
  bool borderIsEmpty = false;
  Maybe<nsCSSBorderRenderer> br =
      nsCSSRendering::CreateNullBorderRendererWithStyleBorder(
          aForFrame->PresContext(), nullptr, aForFrame, nsRect(), aBorderArea,
          aStyleBorder, aForFrame->Style(), &borderIsEmpty,
          aForFrame->GetSkipSides());
  if (!borderIsEmpty && br) {
    br->CreateWebRenderCommands(aItem, aBuilder, aResources, aSc);
  }
}

ImgDrawResult nsCSSRendering::CreateWebRenderCommandsForBorderWithStyleBorder(
    nsDisplayItem* aItem, nsIFrame* aForFrame, const nsRect& aBorderArea,
    mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager,
    nsDisplayListBuilder* aDisplayListBuilder,
    const nsStyleBorder& aStyleBorder) {
  auto& borderImage = aStyleBorder.mBorderImageSource;
  // First try to create commands for simple borders.
  if (borderImage.IsNone()) {
    CreateWebRenderCommandsForNullBorder(
        aItem, aForFrame, aBorderArea, aBuilder, aResources, aSc, aStyleBorder);
    return ImgDrawResult::SUCCESS;
  }

  // Next we try image and gradient borders. Gradients are not supported at
  // this very moment.
  if (!borderImage.IsImageRequestType()) {
    return ImgDrawResult::NOT_SUPPORTED;
  }

  if (aStyleBorder.mBorderImageRepeat._0 ==
          StyleBorderImageRepeatKeyword::Space ||
      aStyleBorder.mBorderImageRepeat._1 ==
          StyleBorderImageRepeatKeyword::Space) {
    return ImgDrawResult::NOT_SUPPORTED;
  }

  uint32_t flags = 0;
  if (aDisplayListBuilder->IsPaintingToWindow()) {
    flags |= nsImageRenderer::FLAG_PAINTING_TO_WINDOW;
  }
  if (aDisplayListBuilder->ShouldSyncDecodeImages()) {
    flags |= nsImageRenderer::FLAG_SYNC_DECODE_IMAGES;
  }

  bool dummy;
  image::ImgDrawResult result;
  Maybe<nsCSSBorderImageRenderer> bir =
      nsCSSBorderImageRenderer::CreateBorderImageRenderer(
          aForFrame->PresContext(), aForFrame, aBorderArea, aStyleBorder,
          aItem->GetBounds(aDisplayListBuilder, &dummy),
          aForFrame->GetSkipSides(), flags, &result);

  if (!bir) {
    // We aren't ready. Try to fallback to the null border image if present but
    // return the draw result for the border image renderer.
    CreateWebRenderCommandsForNullBorder(
        aItem, aForFrame, aBorderArea, aBuilder, aResources, aSc, aStyleBorder);
    return result;
  }

  return bir->CreateWebRenderCommands(aItem, aForFrame, aBuilder, aResources,
                                      aSc, aManager, aDisplayListBuilder);
}

static nsCSSBorderRenderer ConstructBorderRenderer(
    nsPresContext* aPresContext, ComputedStyle* aStyle, DrawTarget* aDrawTarget,
    nsIFrame* aForFrame, const nsRect& aDirtyRect, const nsRect& aBorderArea,
    const nsStyleBorder& aStyleBorder, Sides aSkipSides, bool* aNeedsClip) {
  nsMargin border = aStyleBorder.GetComputedBorder();

  // Compute the outermost boundary of the area that might be painted.
  // Same coordinate space as aBorderArea & aBGClipRect.
  nsRect joinedBorderArea = nsCSSRendering::BoxDecorationRectForBorder(
      aForFrame, aBorderArea, aSkipSides, &aStyleBorder);
  RectCornerRadii bgRadii;
  ::GetRadii(aForFrame, aStyleBorder, aBorderArea, joinedBorderArea, &bgRadii);

  PrintAsFormatString(" joinedBorderArea: %d %d %d %d\n", joinedBorderArea.x,
                      joinedBorderArea.y, joinedBorderArea.width,
                      joinedBorderArea.height);

  // start drawing
  if (nsCSSRendering::IsBoxDecorationSlice(aStyleBorder)) {
    if (joinedBorderArea.IsEqualEdges(aBorderArea)) {
      // No need for a clip, just skip the sides we don't want.
      border.ApplySkipSides(aSkipSides);
    } else {
      // We're drawing borders around the joined continuation boxes so we need
      // to clip that to the slice that we want for this frame.
      *aNeedsClip = true;
    }
  } else {
    MOZ_ASSERT(joinedBorderArea.IsEqualEdges(aBorderArea),
               "Should use aBorderArea for box-decoration-break:clone");
    MOZ_ASSERT(
        aForFrame->GetSkipSides().IsEmpty() ||
            aForFrame->IsTrueOverflowContainer() ||
            aForFrame->IsColumnSetFrame(),  // a little broader than column-rule
        "Should not skip sides for box-decoration-break:clone except "
        "::first-letter/line continuations or other frame types that "
        "don't have borders but those shouldn't reach this point. "
        "Overflow containers do reach this point though, as does "
        "column-rule drawing (which always involves a columnset).");
    border.ApplySkipSides(aSkipSides);
  }

  // Convert to dev pixels.
  nscoord oneDevPixel = aPresContext->DevPixelsToAppUnits(1);
  Rect joinedBorderAreaPx = NSRectToRect(joinedBorderArea, oneDevPixel);
  Margin borderWidths(
      Float(border.top) / oneDevPixel, Float(border.right) / oneDevPixel,
      Float(border.bottom) / oneDevPixel, Float(border.left) / oneDevPixel);
  Rect dirtyRect = NSRectToRect(aDirtyRect, oneDevPixel);

  StyleBorderStyle borderStyles[4];
  nscolor borderColors[4];

  // pull out styles, colors
  for (const auto i : mozilla::AllPhysicalSides()) {
    borderStyles[i] = aStyleBorder.GetBorderStyle(i);
    borderColors[i] = aStyleBorder.BorderColorFor(i).CalcColor(*aStyle);
  }

  PrintAsFormatString(
      " borderStyles: %d %d %d %d\n", static_cast<int>(borderStyles[0]),
      static_cast<int>(borderStyles[1]), static_cast<int>(borderStyles[2]),
      static_cast<int>(borderStyles[3]));

  return nsCSSBorderRenderer(
      aPresContext, aDrawTarget, dirtyRect, joinedBorderAreaPx, borderStyles,
      borderWidths, bgRadii, /* aInset = */ Margin(), borderColors,
      !aForFrame->BackfaceIsHidden(),
      *aNeedsClip ? Some(NSRectToRect(aBorderArea, oneDevPixel)) : Nothing());
}

ImgDrawResult nsCSSRendering::PaintBorderWithStyleBorder(
    nsPresContext* aPresContext, gfxContext& aRenderingContext,
    nsIFrame* aForFrame, const nsRect& aDirtyRect, const nsRect& aBorderArea,
    const nsStyleBorder& aStyleBorder, ComputedStyle* aStyle,
    PaintBorderFlags aFlags, Sides aSkipSides) {
  DrawTarget& aDrawTarget = *aRenderingContext.GetDrawTarget();

  PrintAsStringNewline("++ PaintBorder");

  // Check to see if we have an appearance defined.  If so, we let the theme
  // renderer draw the border.  DO not get the data from aForFrame, since the
  // passed in ComputedStyle may be different!  Always use |aStyle|!
  StyleAppearance appearance = aStyle->StyleDisplay()->EffectiveAppearance();
  if (appearance != StyleAppearance::None) {
    nsITheme* theme = aPresContext->Theme();
    if (theme->ThemeSupportsWidget(aPresContext, aForFrame, appearance)) {
      return ImgDrawResult::SUCCESS;  // Let the theme handle it.
    }
  }

  if (!aStyleBorder.mBorderImageSource.IsNone()) {
    ImgDrawResult result = ImgDrawResult::SUCCESS;

    uint32_t irFlags = 0;
    if (aFlags & PaintBorderFlags::SyncDecodeImages) {
      irFlags |= nsImageRenderer::FLAG_SYNC_DECODE_IMAGES;
    }

    // Creating the border image renderer will request a decode, and we rely on
    // that happening.
    Maybe<nsCSSBorderImageRenderer> renderer =
        nsCSSBorderImageRenderer::CreateBorderImageRenderer(
            aPresContext, aForFrame, aBorderArea, aStyleBorder, aDirtyRect,
            aSkipSides, irFlags, &result);
    // renderer was created successfully, which means border image is ready to
    // be used.
    if (renderer) {
      MOZ_ASSERT(result == ImgDrawResult::SUCCESS);
      return renderer->DrawBorderImage(aPresContext, aRenderingContext,
                                       aForFrame, aDirtyRect);
    }
  }

  ImgDrawResult result = ImgDrawResult::SUCCESS;

  // If we had a border-image, but it wasn't loaded, then we should return
  // ImgDrawResult::NOT_READY; we'll want to try again if we do a paint with
  // sync decoding enabled.
  if (!aStyleBorder.mBorderImageSource.IsNone()) {
    result = ImgDrawResult::NOT_READY;
  }

  nsMargin border = aStyleBorder.GetComputedBorder();
  if (0 == border.left && 0 == border.right && 0 == border.top &&
      0 == border.bottom) {
    // Empty border area
    return result;
  }

  bool needsClip = false;
  nsCSSBorderRenderer br = ConstructBorderRenderer(
      aPresContext, aStyle, &aDrawTarget, aForFrame, aDirtyRect, aBorderArea,
      aStyleBorder, aSkipSides, &needsClip);
  if (needsClip) {
    aDrawTarget.PushClipRect(NSRectToSnappedRect(
        aBorderArea, aForFrame->PresContext()->AppUnitsPerDevPixel(),
        aDrawTarget));
  }

  br.DrawBorders();

  if (needsClip) {
    aDrawTarget.PopClip();
  }

  PrintAsStringNewline();

  return result;
}

Maybe<nsCSSBorderRenderer> nsCSSRendering::CreateBorderRendererWithStyleBorder(
    nsPresContext* aPresContext, DrawTarget* aDrawTarget, nsIFrame* aForFrame,
    const nsRect& aDirtyRect, const nsRect& aBorderArea,
    const nsStyleBorder& aStyleBorder, ComputedStyle* aStyle,
    bool* aOutBorderIsEmpty, Sides aSkipSides) {
  if (!aStyleBorder.mBorderImageSource.IsNone()) {
    return Nothing();
  }
  return CreateNullBorderRendererWithStyleBorder(
      aPresContext, aDrawTarget, aForFrame, aDirtyRect, aBorderArea,
      aStyleBorder, aStyle, aOutBorderIsEmpty, aSkipSides);
}

Maybe<nsCSSBorderRenderer>
nsCSSRendering::CreateNullBorderRendererWithStyleBorder(
    nsPresContext* aPresContext, DrawTarget* aDrawTarget, nsIFrame* aForFrame,
    const nsRect& aDirtyRect, const nsRect& aBorderArea,
    const nsStyleBorder& aStyleBorder, ComputedStyle* aStyle,
    bool* aOutBorderIsEmpty, Sides aSkipSides) {
  StyleAppearance appearance = aStyle->StyleDisplay()->EffectiveAppearance();
  if (appearance != StyleAppearance::None) {
    nsITheme* theme = aPresContext->Theme();
    if (theme->ThemeSupportsWidget(aPresContext, aForFrame, appearance)) {
      // The border will be draw as part of the themed background item created
      // for this same frame. If no themed background item was created then not
      // drawing also matches that we do without webrender and what
      // nsDisplayBorder does for themed borders.
      if (aOutBorderIsEmpty) {
        *aOutBorderIsEmpty = true;
      }
      return Nothing();
    }
  }

  nsMargin border = aStyleBorder.GetComputedBorder();
  if (0 == border.left && 0 == border.right && 0 == border.top &&
      0 == border.bottom) {
    // Empty border area
    if (aOutBorderIsEmpty) {
      *aOutBorderIsEmpty = true;
    }
    return Nothing();
  }

  bool needsClip = false;
  nsCSSBorderRenderer br = ConstructBorderRenderer(
      aPresContext, aStyle, aDrawTarget, aForFrame, aDirtyRect, aBorderArea,
      aStyleBorder, aSkipSides, &needsClip);
  return Some(br);
}

Maybe<nsCSSBorderRenderer>
nsCSSRendering::CreateBorderRendererForNonThemedOutline(
    nsPresContext* aPresContext, DrawTarget* aDrawTarget, nsIFrame* aForFrame,
    const nsRect& aDirtyRect, const nsRect& aInnerRect, ComputedStyle* aStyle) {
  // Get our ComputedStyle's color struct.
  const nsStyleOutline* ourOutline = aStyle->StyleOutline();
  if (!ourOutline->ShouldPaintOutline()) {
    // Empty outline
    return Nothing();
  }

  nsRect innerRect = aInnerRect;

  const nsSize effectiveOffset = ourOutline->EffectiveOffsetFor(innerRect);
  innerRect.Inflate(effectiveOffset);

  // If the dirty rect is completely inside the border area (e.g., only the
  // content is being painted), then we can skip out now
  // XXX this isn't exactly true for rounded borders, where the inside curves
  // may encroach into the content area.  A safer calculation would be to
  // shorten insideRect by the radius one each side before performing this test.
  if (innerRect.Contains(aDirtyRect)) {
    return Nothing();
  }

  const nscoord width = ourOutline->mOutlineWidth;

  StyleBorderStyle outlineStyle;
  // Themed outlines are handled by our callers, if supported.
  if (ourOutline->mOutlineStyle.IsAuto()) {
    if (width == 0) {
      return Nothing();  // empty outline
    }
    // http://dev.w3.org/csswg/css-ui/#outline
    // "User agents may treat 'auto' as 'solid'."
    outlineStyle = StyleBorderStyle::Solid;
  } else {
    outlineStyle = ourOutline->mOutlineStyle.AsBorderStyle();
  }

  RectCornerRadii outlineRadii;
  nsRect outerRect = innerRect;
  outerRect.Inflate(width);

  const nscoord oneDevPixel = aPresContext->AppUnitsPerDevPixel();
  Rect oRect(NSRectToRect(outerRect, oneDevPixel));

  const Margin outlineWidths(
      Float(width) / oneDevPixel, Float(width) / oneDevPixel,
      Float(width) / oneDevPixel, Float(width) / oneDevPixel);

  Margin outlineInset;

  // convert the radii
  nsRectCornerRadii twipsRadii;

  // get the radius for our outline
  if (aForFrame->GetBorderRadii(twipsRadii)) {
    RectCornerRadii innerRadii;
    ComputePixelRadii(twipsRadii, oneDevPixel, &innerRadii);

    const auto devPxOffset = LayoutDeviceSize::FromAppUnits(
        effectiveOffset, aPresContext->AppUnitsPerDevPixel());

    const Margin widths(outlineWidths.top + devPxOffset.Height(),
                        outlineWidths.right + devPxOffset.Width(),
                        outlineWidths.bottom + devPxOffset.Height(),
                        outlineWidths.left + devPxOffset.Width());

    // Pass down the computed inset for the correct rendering of
    // corner-shape contoured superellipses.
    outlineInset = -widths;

    nsCSSBorderRenderer::ComputeOuterRadii(innerRadii, widths, &outlineRadii);
  }

  StyleBorderStyle outlineStyles[4] = {outlineStyle, outlineStyle, outlineStyle,
                                       outlineStyle};

  // This handles treating the initial color as 'currentColor'; if we
  // ever want 'invert' back we'll need to do a bit of work here too.
  nscolor outlineColor =
      aStyle->GetVisitedDependentColor(&nsStyleOutline::mOutlineColor);
  nscolor outlineColors[4] = {outlineColor, outlineColor, outlineColor,
                              outlineColor};

  Rect dirtyRect = NSRectToRect(aDirtyRect, oneDevPixel);

  return Some(nsCSSBorderRenderer(aPresContext, aDrawTarget, dirtyRect, oRect,
                                  outlineStyles, outlineWidths, outlineRadii,
                                  outlineInset, outlineColors,
                                  !aForFrame->BackfaceIsHidden(), Nothing()));
}

void nsCSSRendering::PaintNonThemedOutline(nsPresContext* aPresContext,
                                           gfxContext& aRenderingContext,
                                           nsIFrame* aForFrame,
                                           const nsRect& aDirtyRect,
                                           const nsRect& aInnerRect,
                                           ComputedStyle* aStyle) {
  Maybe<nsCSSBorderRenderer> br = CreateBorderRendererForNonThemedOutline(
      aPresContext, aRenderingContext.GetDrawTarget(), aForFrame, aDirtyRect,
      aInnerRect, aStyle);
  if (!br) {
    return;
  }

  // start drawing
  br->DrawBorders();

  PrintAsStringNewline();
}

nsCSSBorderRenderer nsCSSRendering::GetBorderRendererForFocus(
    nsIFrame* aForFrame, DrawTarget* aDrawTarget, const nsRect& aFocusRect,
    nscolor aColor) {
  auto* pc = aForFrame->PresContext();
  nscoord oneCSSPixel = nsPresContext::CSSPixelsToAppUnits(1);
  nscoord oneDevPixel = pc->DevPixelsToAppUnits(1);

  Rect focusRect(NSRectToRect(aFocusRect, oneDevPixel));

  RectCornerRadii focusRadii;
  Margin focusWidths(
      Float(oneCSSPixel) / oneDevPixel, Float(oneCSSPixel) / oneDevPixel,
      Float(oneCSSPixel) / oneDevPixel, Float(oneCSSPixel) / oneDevPixel);

  StyleBorderStyle focusStyles[4] = {
      StyleBorderStyle::Dotted, StyleBorderStyle::Dotted,
      StyleBorderStyle::Dotted, StyleBorderStyle::Dotted};
  nscolor focusColors[4] = {aColor, aColor, aColor, aColor};

  // Because this renders a dotted border, the background color
  // should not be used.  Therefore, we provide a value that will
  // be blatantly wrong if it ever does get used.  (If this becomes
  // something that CSS can style, this function will then have access
  // to a ComputedStyle and can use the same logic that PaintBorder
  // and PaintOutline do.)
  return nsCSSBorderRenderer(pc, aDrawTarget, focusRect, focusRect, focusStyles,
                             focusWidths, focusRadii, /* aInset = */ Margin(),
                             focusColors, !aForFrame->BackfaceIsHidden(),
                             Nothing());
}

// Thebes Border Rendering Code End
//----------------------------------------------------------------------

//----------------------------------------------------------------------

/**
 * Helper for ComputeObjectAnchorPoint; parameters are the same as for
 * that function, except they're for a single coordinate / a single size
 * dimension. (so, x/width vs. y/height)
 */
static void ComputeObjectAnchorCoord(const LengthPercentage& aCoord,
                                     const nscoord aOriginBounds,
                                     const nscoord aImageSize,
                                     nscoord* aTopLeftCoord,
                                     nscoord* aAnchorPointCoord) {
  nscoord extraSpace = aOriginBounds - aImageSize;

  // The anchor-point doesn't care about our image's size; just the size
  // of the region we're rendering into.
  *aAnchorPointCoord = aCoord.Resolve(
      aOriginBounds, static_cast<nscoord (*)(float)>(NSToCoordRoundWithClamp));
  // Adjust aTopLeftCoord by the specified % of the extra space.
  *aTopLeftCoord = aCoord.Resolve(
      extraSpace, static_cast<nscoord (*)(float)>(NSToCoordRoundWithClamp));
}

void nsImageRenderer::ComputeObjectAnchorPoint(const Position& aPos,
                                               const nsSize& aOriginBounds,
                                               const nsSize& aImageSize,
                                               nsPoint* aTopLeft,
                                               nsPoint* aAnchorPoint) {
  ComputeObjectAnchorCoord(aPos.horizontal, aOriginBounds.width,
                           aImageSize.width, &aTopLeft->x, &aAnchorPoint->x);

  ComputeObjectAnchorCoord(aPos.vertical, aOriginBounds.height,
                           aImageSize.height, &aTopLeft->y, &aAnchorPoint->y);
}

// In print / print preview we have multiple canvas frames (one for each page,
// and one for the document as a whole). For the topmost one, we really want the
// page sequence page background, not the root or body's background.
static nsIFrame* GetPageSequenceForCanvas(const nsIFrame* aCanvasFrame) {
  MOZ_ASSERT(aCanvasFrame->IsCanvasFrame(), "not a canvas frame");
  nsPresContext* pc = aCanvasFrame->PresContext();
  if (!pc->IsRootPaginatedDocument()) {
    return nullptr;
  }
  auto* ps = pc->PresShell()->GetPageSequenceFrame();
  if (NS_WARN_IF(!ps)) {
    return nullptr;
  }
  if (ps->GetParent() != aCanvasFrame) {
    return nullptr;
  }
  return ps;
}

auto nsCSSRendering::FindEffectiveBackgroundColor(nsIFrame* aFrame,
                                                  bool aStopAtThemed,
                                                  bool aPreferBodyToCanvas)
    -> EffectiveBackgroundColor {
  MOZ_ASSERT(aFrame);
  nsPresContext* pc = aFrame->PresContext();
  auto BgColorIfNotTransparent = [&](nsIFrame* aFrame) -> Maybe<nscolor> {
    nscolor c =
        aFrame->GetVisitedDependentColor(&nsStyleBackground::mBackgroundColor);
    if (NS_GET_A(c) == 255) {
      return Some(c);
    }
    if (NS_GET_A(c)) {
      // TODO(emilio): We should maybe just blend with ancestor bg colors and
      // such, but this is probably good enough for now, matches pre-existing
      // behavior.
      const nscolor defaultBg = pc->DefaultBackgroundColor();
      MOZ_ASSERT(NS_GET_A(defaultBg) == 255, "PreferenceSheet guarantees this");
      return Some(NS_ComposeColors(defaultBg, c));
    }
    return Nothing();
  };

  for (nsIFrame* frame = aFrame; frame;
       frame = nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(frame)) {
    if (auto bg = BgColorIfNotTransparent(frame)) {
      return {*bg};
    }

    if (aStopAtThemed && frame->IsThemed()) {
      return {NS_TRANSPARENT, true};
    }

    if (frame->IsCanvasFrame()) {
      if (aPreferBodyToCanvas && !GetPageSequenceForCanvas(frame)) {
        if (auto* body = pc->Document()->GetBodyElement()) {
          if (nsIFrame* f = body->GetPrimaryFrame()) {
            if (auto bg = BgColorIfNotTransparent(f)) {
              return {*bg};
            }
          }
        }
      }
      if (nsIFrame* bgFrame = FindBackgroundFrame(frame)) {
        if (auto bg = BgColorIfNotTransparent(bgFrame)) {
          return {*bg};
        }
      }
    }
  }

  return {pc->DefaultBackgroundColor()};
}

nsIFrame* nsCSSRendering::FindBackgroundStyleFrame(nsIFrame* aForFrame) {
  const nsStyleBackground* result = aForFrame->StyleBackground();

  // Check if we need to do propagation from BODY rather than HTML.
  if (!result->IsTransparent(aForFrame)) {
    return aForFrame;
  }

  nsIContent* content = aForFrame->GetContent();
  // The root element content can't be null. We wouldn't know what
  // frame to create for aFrame.
  // Use |OwnerDoc| so it works during destruction.
  if (!content) {
    return aForFrame;
  }

  Document* document = content->OwnerDoc();

  dom::Element* bodyContent = document->GetBodyElement();
  // We need to null check the body node (bug 118829) since
  // there are cases, thanks to the fix for bug 5569, where we
  // will reflow a document with no body.  In particular, if a
  // SCRIPT element in the head blocks the parser and then has a
  // SCRIPT that does "document.location.href = 'foo'", then
  // nsParser::Terminate will call |DidBuildModel| methods
  // through to the content sink, which will call |StartLayout|
  // and thus |Initialize| on the pres shell.  See bug 119351
  // for the ugly details.
  if (!bodyContent || aForFrame->StyleDisplay()->IsContainAny()) {
    return aForFrame;
  }

  nsIFrame* bodyFrame = bodyContent->GetPrimaryFrame();
  if (!bodyFrame || bodyFrame->StyleDisplay()->IsContainAny()) {
    return aForFrame;
  }

  return nsLayoutUtils::GetStyleFrame(bodyFrame);
}

/**
 * |FindBackground| finds the correct style data to use to paint the
 * background.  It is responsible for handling the following two
 * statements in section 14.2 of CSS2:
 *
 *   The background of the box generated by the root element covers the
 *   entire canvas.
 *
 *   For HTML documents, however, we recommend that authors specify the
 *   background for the BODY element rather than the HTML element. User
 *   agents should observe the following precedence rules to fill in the
 *   background: if the value of the 'background' property for the HTML
 *   element is different from 'transparent' then use it, else use the
 *   value of the 'background' property for the BODY element. If the
 *   resulting value is 'transparent', the rendering is undefined.
 *
 * Thus, in our implementation, it is responsible for ensuring that:
 *  + we paint the correct background on the |nsCanvasFrame| or |nsPageFrame|,
 *  + we don't paint the background on the root element, and
 *  + we don't paint the background on the BODY element in *some* cases,
 *    and for SGML-based HTML documents only.
 *
 * |FindBackground| checks whether a background should be painted. If yes, it
 * returns the resulting ComputedStyle to use for the background information;
 * Otherwise, it returns nullptr.
 */
ComputedStyle* nsCSSRendering::FindRootFrameBackground(nsIFrame* aForFrame) {
  return FindBackgroundStyleFrame(aForFrame)->Style();
}

static nsIFrame* FindCanvasBackgroundFrame(const nsIFrame* aForFrame,
                                           nsIFrame* aRootElementFrame) {
  MOZ_ASSERT(aForFrame->IsCanvasFrame(), "not a canvas frame");
  if (auto* ps = GetPageSequenceForCanvas(aForFrame)) {
    return ps;
  }
  if (aRootElementFrame) {
    return nsCSSRendering::FindBackgroundStyleFrame(aRootElementFrame);
  }
  // This should always give transparent, so we'll fill it in with the default
  // color if needed.  This seems to happen a bit while a page is being loaded.
  return const_cast<nsIFrame*>(aForFrame);
}

// Helper for FindBackgroundFrame. Returns true if aForFrame has a meaningful
// background that it should draw (i.e. that it hasn't propagated to another
// frame).  See documentation for FindBackground.
inline bool FrameHasMeaningfulBackground(const nsIFrame* aForFrame,
                                         nsIFrame* aRootElementFrame) {
  MOZ_ASSERT(!aForFrame->IsCanvasFrame(),
             "FindBackgroundFrame handles canvas frames before calling us, "
             "so we don't need to consider them here");

  if (aForFrame == aRootElementFrame) {
    // We must have propagated our background to the viewport or canvas. Abort.
    return false;
  }

  // Return true unless the frame is for a BODY element whose background
  // was propagated to the viewport.

  nsIContent* content = aForFrame->GetContent();
  if (!content || content->NodeInfo()->NameAtom() != nsGkAtoms::body) {
    return true;  // not frame for a "body" element
  }
  // It could be a non-HTML "body" element but that's OK, we'd fail the
  // bodyContent check below

  if (aForFrame->Style()->GetPseudoType() != PseudoStyleType::NotPseudo ||
      aForFrame->StyleDisplay()->IsContainAny()) {
    return true;  // A pseudo-element frame, or contained.
  }

  // We should only look at the <html> background if we're in an HTML document
  Document* document = content->OwnerDoc();

  dom::Element* bodyContent = document->GetBodyElement();
  if (bodyContent != content) {
    return true;  // this wasn't the background that was propagated
  }

  // This can be called even when there's no root element yet, during frame
  // construction, via nsLayoutUtils::FrameHasTransparency and
  // nsContainerFrame::SyncFrameViewProperties.
  if (!aRootElementFrame || aRootElementFrame->StyleDisplay()->IsContainAny()) {
    return true;
  }

  const nsStyleBackground* htmlBG = aRootElementFrame->StyleBackground();
  return !htmlBG->IsTransparent(aRootElementFrame);
}

nsIFrame* nsCSSRendering::FindBackgroundFrame(const nsIFrame* aForFrame) {
  nsIFrame* rootElementFrame =
      aForFrame->PresShell()->FrameConstructor()->GetRootElementStyleFrame();
  if (aForFrame->IsCanvasFrame()) {
    return FindCanvasBackgroundFrame(aForFrame, rootElementFrame);
  }

  if (FrameHasMeaningfulBackground(aForFrame, rootElementFrame)) {
    return const_cast<nsIFrame*>(aForFrame);
  }

  return nullptr;
}

ComputedStyle* nsCSSRendering::FindBackground(const nsIFrame* aForFrame) {
  if (auto* backgroundFrame = FindBackgroundFrame(aForFrame)) {
    return backgroundFrame->Style();
  }
  return nullptr;
}

void nsCSSRendering::PresShellChanged() {
  if (gInlineBGData) {
    gInlineBGData->Reset();
  }
}

bool nsCSSRendering::HasBoxShadowNativeTheme(nsIFrame* aFrame,
                                             bool& aMaybeHasBorderRadius) {
  const nsStyleDisplay* styleDisplay = aFrame->StyleDisplay();
  nsITheme::Transparency transparency;
  if (aFrame->IsThemed(styleDisplay, &transparency)) {
    aMaybeHasBorderRadius = false;
    // For opaque (rectangular) theme widgets we can take the generic
    // border-box path with border-radius disabled.
    return transparency != nsITheme::eOpaque;
  }

  aMaybeHasBorderRadius = true;
  return false;
}

gfx::sRGBColor nsCSSRendering::GetShadowColor(const StyleSimpleShadow& aShadow,
                                              nsIFrame* aFrame,
                                              float aOpacity) {
  // Get the shadow color; if not specified, use the foreground color
  nscolor shadowColor = aShadow.color.CalcColor(aFrame);
  sRGBColor color = sRGBColor::FromABGR(shadowColor);
  color.a *= aOpacity;
  return color;
}

nsRect nsCSSRendering::GetShadowRect(const nsRect& aFrameArea,
                                     bool aNativeTheme, nsIFrame* aForFrame) {
  nsRect frameRect = aNativeTheme ? aForFrame->InkOverflowRectRelativeToSelf() +
                                        aFrameArea.TopLeft()
                                  : aFrameArea;
  Sides skipSides = aForFrame->GetSkipSides();
  frameRect = BoxDecorationRectForBorder(aForFrame, frameRect, skipSides);

  // Explicitly do not need to account for the spread radius here
  // Webrender does it for us or PaintBoxShadow will for non-WR
  return frameRect;
}

bool nsCSSRendering::GetBorderRadii(const nsRect& aFrameRect,
                                    const nsRect& aBorderRect, nsIFrame* aFrame,
                                    RectCornerRadii& aOutRadii) {
  const nscoord oneDevPixel = aFrame->PresContext()->DevPixelsToAppUnits(1);
  nsRectCornerRadii twipsRadii;
  NS_ASSERTION(
      aBorderRect.Size() == aFrame->VisualBorderRectRelativeToSelf().Size(),
      "unexpected size");
  nsSize sz = aFrameRect.Size();
  bool hasBorderRadius = aFrame->GetBorderRadii(sz, sz, Sides(), twipsRadii);
  if (hasBorderRadius) {
    ComputePixelRadii(twipsRadii, oneDevPixel, &aOutRadii);
  }

  return hasBorderRadius;
}

void nsCSSRendering::PaintBoxShadowOuter(nsPresContext* aPresContext,
                                         gfxContext& aRenderingContext,
                                         nsIFrame* aForFrame,
                                         const nsRect& aFrameArea,
                                         const nsRect& aDirtyRect,
                                         float aOpacity) {
  DrawTarget& aDrawTarget = *aRenderingContext.GetDrawTarget();
  auto shadows = aForFrame->StyleEffects()->mBoxShadow.AsSpan();
  if (shadows.IsEmpty()) {
    return;
  }

  bool hasBorderRadius;
  // mutually exclusive with hasBorderRadius
  bool nativeTheme = HasBoxShadowNativeTheme(aForFrame, hasBorderRadius);
  const nsStyleDisplay* styleDisplay = aForFrame->StyleDisplay();

  nsRect frameRect = GetShadowRect(aFrameArea, nativeTheme, aForFrame);

  // Get any border radius, since box-shadow must also have rounded corners if
  // the frame does.
  RectCornerRadii borderRadii;
  const nscoord oneDevPixel = aPresContext->DevPixelsToAppUnits(1);
  if (hasBorderRadius) {
    nsRectCornerRadii twipsRadii;
    NS_ASSERTION(
        aFrameArea.Size() == aForFrame->VisualBorderRectRelativeToSelf().Size(),
        "unexpected size");
    nsSize sz = frameRect.Size();
    hasBorderRadius = aForFrame->GetBorderRadii(sz, sz, Sides(), twipsRadii);
    if (hasBorderRadius) {
      ComputePixelRadii(twipsRadii, oneDevPixel, &borderRadii);
    }
  }

  // We don't show anything that intersects with the frame we're blurring on. So
  // tell the blurrer not to do unnecessary work there.
  gfxRect skipGfxRect = ThebesRect(NSRectToRect(frameRect, oneDevPixel));
  skipGfxRect.Round();
  bool useSkipGfxRect = true;
  if (nativeTheme) {
    // Optimize non-leaf native-themed frames by skipping computing pixels
    // in the padding-box. We assume the padding-box is going to be painted
    // opaquely for non-leaf frames.
    // XXX this may not be a safe assumption; we should make this go away
    // by optimizing box-shadow drawing more for the cases where we don't have a
    // skip-rect.
    useSkipGfxRect = !aForFrame->IsLeaf();
    nsRect paddingRect =
        aForFrame->GetPaddingRectRelativeToSelf() + aFrameArea.TopLeft();
    skipGfxRect = nsLayoutUtils::RectToGfxRect(paddingRect, oneDevPixel);
  } else if (hasBorderRadius) {
    skipGfxRect.Deflate(gfxMargin(
        std::max(borderRadii[C_TL].height, borderRadii[C_TR].height), 0,
        std::max(borderRadii[C_BL].height, borderRadii[C_BR].height), 0));
  }

  for (const StyleBoxShadow& shadow : Reversed(shadows)) {
    if (shadow.inset) {
      continue;
    }

    nsRect shadowRect = frameRect;
    nsPoint shadowOffset(shadow.base.horizontal.ToAppUnits(),
                         shadow.base.vertical.ToAppUnits());
    shadowRect.MoveBy(shadowOffset);
    nscoord shadowSpread = shadow.spread.ToAppUnits();
    if (!nativeTheme) {
      shadowRect.Inflate(shadowSpread);
    }

    // shadowRect won't include the blur, so make an extra rect here that
    // includes the blur for use in the even-odd rule below.
    nsRect shadowRectPlusBlur = shadowRect;
    nscoord blurRadius = shadow.base.blur.ToAppUnits();
    shadowRectPlusBlur.Inflate(
        nsContextBoxBlur::GetBlurRadiusMargin(blurRadius, oneDevPixel));

    Rect shadowGfxRectPlusBlur = NSRectToRect(shadowRectPlusBlur, oneDevPixel);
    shadowGfxRectPlusBlur.RoundOut();
    MaybeSnapToDevicePixels(shadowGfxRectPlusBlur, aDrawTarget, true);

    sRGBColor gfxShadowColor = GetShadowColor(shadow.base, aForFrame, aOpacity);

    if (nativeTheme) {
      nsContextBoxBlur blurringArea;

      // When getting the widget shape from the native theme, we're going
      // to draw the widget into the shadow surface to create a mask.
      // We need to ensure that there actually *is* a shadow surface
      // and that we're not going to draw directly into aRenderingContext.
      gfxContext* shadowContext = blurringArea.Init(
          shadowRect, shadowSpread, blurRadius, oneDevPixel, &aRenderingContext,
          aDirtyRect, useSkipGfxRect ? &skipGfxRect : nullptr,
          nsContextBoxBlur::FORCE_MASK);
      if (!shadowContext) {
        continue;
      }

      MOZ_ASSERT(shadowContext == blurringArea.GetContext());

      aRenderingContext.Save();
      aRenderingContext.SetColor(gfxShadowColor);

      // Draw the shape of the frame so it can be blurred. Recall how
      // nsContextBoxBlur doesn't make any temporary surfaces if blur is 0 and
      // it just returns the original surface? If we have no blur, we're
      // painting this fill on the actual content surface (aRenderingContext ==
      // shadowContext) which is why we set up the color and clip before doing
      // this.

      // We don't clip the border-box from the shadow, nor any other box.
      // We assume that the native theme is going to paint over the shadow.

      // Draw the widget shape
      gfxContextMatrixAutoSaveRestore save(shadowContext);
      gfxPoint devPixelOffset = nsLayoutUtils::PointToGfxPoint(
          shadowOffset, aPresContext->AppUnitsPerDevPixel());
      shadowContext->SetMatrixDouble(
          shadowContext->CurrentMatrixDouble().PreTranslate(devPixelOffset));

      nsRect nativeRect = aDirtyRect;
      nativeRect.MoveBy(-shadowOffset);
      nativeRect.IntersectRect(frameRect, nativeRect);
      aPresContext->Theme()->DrawWidgetBackground(
          shadowContext, aForFrame, styleDisplay->EffectiveAppearance(),
          aFrameArea, nativeRect, nsITheme::DrawOverflow::No);

      blurringArea.DoPaint();
      aRenderingContext.Restore();
    } else {
      aRenderingContext.Save();

      {
        Rect innerClipRect = NSRectToRect(frameRect, oneDevPixel);
        if (!MaybeSnapToDevicePixels(innerClipRect, aDrawTarget, true)) {
          innerClipRect.Round();
        }

        // Clip out the interior of the frame's border edge so that the shadow
        // is only painted outside that area.
        RefPtr<PathBuilder> builder =
            aDrawTarget.CreatePathBuilder(FillRule::FILL_EVEN_ODD);
        AppendRectToPath(builder, shadowGfxRectPlusBlur);
        if (hasBorderRadius) {
          AppendRoundedRectToPath(builder, innerClipRect, borderRadii);
        } else {
          AppendRectToPath(builder, innerClipRect);
        }
        RefPtr<Path> path = builder->Finish();
        aRenderingContext.Clip(path);
      }

      // Clip the shadow so that we only get the part that applies to aForFrame.
      nsRect fragmentClip = shadowRectPlusBlur;
      Sides skipSides = aForFrame->GetSkipSides();
      if (!skipSides.IsEmpty()) {
        if (skipSides.Left()) {
          nscoord xmost = fragmentClip.XMost();
          fragmentClip.x = aFrameArea.x;
          fragmentClip.width = xmost - fragmentClip.x;
        }
        if (skipSides.Right()) {
          nscoord xmost = fragmentClip.XMost();
          nscoord overflow = xmost - aFrameArea.XMost();
          if (overflow > 0) {
            fragmentClip.width -= overflow;
          }
        }
        if (skipSides.Top()) {
          nscoord ymost = fragmentClip.YMost();
          fragmentClip.y = aFrameArea.y;
          fragmentClip.height = ymost - fragmentClip.y;
        }
        if (skipSides.Bottom()) {
          nscoord ymost = fragmentClip.YMost();
          nscoord overflow = ymost - aFrameArea.YMost();
          if (overflow > 0) {
            fragmentClip.height -= overflow;
          }
        }
      }
      fragmentClip = fragmentClip.Intersect(aDirtyRect);
      aRenderingContext.Clip(NSRectToSnappedRect(
          fragmentClip, aForFrame->PresContext()->AppUnitsPerDevPixel(),
          aDrawTarget));

      RectCornerRadii clipRectRadii;
      if (hasBorderRadius) {
        Float spreadDistance = Float(shadowSpread) / oneDevPixel;
        Margin borderSizes(spreadDistance, spreadDistance, spreadDistance,
                           spreadDistance);
        nsCSSBorderRenderer::ComputeOuterRadii(borderRadii, borderSizes,
                                               &clipRectRadii);
      }
      nsContextBoxBlur::BlurRectangle(
          &aRenderingContext, shadowRect, oneDevPixel,
          hasBorderRadius ? &clipRectRadii : nullptr, blurRadius,
          gfxShadowColor, aDirtyRect, skipGfxRect);
      aRenderingContext.Restore();
    }
  }
}

nsRect nsCSSRendering::GetBoxShadowInnerPaddingRect(nsIFrame* aFrame,
                                                    const nsRect& aFrameArea) {
  Sides skipSides = aFrame->GetSkipSides();
  nsRect frameRect = BoxDecorationRectForBorder(aFrame, aFrameArea, skipSides);

  nsRect paddingRect = frameRect;
  nsMargin border = aFrame->GetUsedBorder();
  paddingRect.Deflate(border);
  return paddingRect;
}

bool nsCSSRendering::ShouldPaintBoxShadowInner(nsIFrame* aFrame) {
  const Span<const StyleBoxShadow> shadows =
      aFrame->StyleEffects()->mBoxShadow.AsSpan();
  if (shadows.IsEmpty()) {
    return false;
  }

  if (aFrame->IsThemed() && aFrame->GetContent() &&
      !nsContentUtils::IsChromeDoc(aFrame->GetContent()->GetComposedDoc())) {
    // There's no way of getting hold of a shape corresponding to a
    // "padding-box" for native-themed widgets, so just don't draw
    // inner box-shadows for them. But we allow chrome to paint inner
    // box shadows since chrome can be aware of the platform theme.
    return false;
  }

  return true;
}

bool nsCSSRendering::GetShadowInnerRadii(nsIFrame* aFrame,
                                         const nsRect& aFrameArea,
                                         RectCornerRadii& aOutInnerRadii) {
  // Get any border radius, since box-shadow must also have rounded corners
  // if the frame does.
  nsRectCornerRadii twipsRadii;
  nsRect frameRect =
      BoxDecorationRectForBorder(aFrame, aFrameArea, aFrame->GetSkipSides());
  nsSize sz = frameRect.Size();
  nsMargin border = aFrame->GetUsedBorder();
  aFrame->GetBorderRadii(sz, sz, Sides(), twipsRadii);
  const nscoord oneDevPixel = aFrame->PresContext()->DevPixelsToAppUnits(1);

  RectCornerRadii borderRadii;

  const bool hasBorderRadius =
      GetBorderRadii(frameRect, aFrameArea, aFrame, borderRadii);

  if (hasBorderRadius) {
    ComputePixelRadii(twipsRadii, oneDevPixel, &borderRadii);

    Margin borderSizes(
        Float(border.top) / oneDevPixel, Float(border.right) / oneDevPixel,
        Float(border.bottom) / oneDevPixel, Float(border.left) / oneDevPixel);
    nsCSSBorderRenderer::ComputeInnerRadii(borderRadii, borderSizes,
                                           &aOutInnerRadii);
  }

  return hasBorderRadius;
}

void nsCSSRendering::PaintBoxShadowInner(nsPresContext* aPresContext,
                                         gfxContext& aRenderingContext,
                                         nsIFrame* aForFrame,
                                         const nsRect& aFrameArea) {
  if (!ShouldPaintBoxShadowInner(aForFrame)) {
    return;
  }

  const Span<const StyleBoxShadow> shadows =
      aForFrame->StyleEffects()->mBoxShadow.AsSpan();
  NS_ASSERTION(
      aForFrame->IsFieldSetFrame() || aFrameArea.Size() == aForFrame->GetSize(),
      "unexpected size");

  nsRect paddingRect = GetBoxShadowInnerPaddingRect(aForFrame, aFrameArea);

  RectCornerRadii innerRadii;
  bool hasBorderRadius = GetShadowInnerRadii(aForFrame, aFrameArea, innerRadii);

  const nscoord oneDevPixel = aPresContext->DevPixelsToAppUnits(1);

  for (const StyleBoxShadow& shadow : Reversed(shadows)) {
    if (!shadow.inset) {
      continue;
    }

    // shadowPaintRect: the area to paint on the temp surface
    // shadowClipRect: the area on the temporary surface within shadowPaintRect
    //                 that we will NOT paint in
    nscoord blurRadius = shadow.base.blur.ToAppUnits();
    nsMargin blurMargin =
        nsContextBoxBlur::GetBlurRadiusMargin(blurRadius, oneDevPixel);
    nsRect shadowPaintRect = paddingRect;
    shadowPaintRect.Inflate(blurMargin);

    // Round the spread radius to device pixels (by truncation).
    // This mostly matches what we do for borders, except that we don't round
    // up values between zero and one device pixels to one device pixel.
    // This way of rounding is symmetric around zero, which makes sense for
    // the spread radius.
    int32_t spreadDistance = shadow.spread.ToAppUnits() / oneDevPixel;
    nscoord spreadDistanceAppUnits =
        aPresContext->DevPixelsToAppUnits(spreadDistance);

    nsRect shadowClipRect = paddingRect;
    shadowClipRect.MoveBy(shadow.base.horizontal.ToAppUnits(),
                          shadow.base.vertical.ToAppUnits());
    shadowClipRect.Deflate(spreadDistanceAppUnits, spreadDistanceAppUnits);

    Rect shadowClipGfxRect = NSRectToRect(shadowClipRect, oneDevPixel);
    shadowClipGfxRect.Round();

    RectCornerRadii clipRectRadii;
    if (hasBorderRadius) {
      // Calculate the radii the inner clipping rect will have
      Margin borderSizes;

      // See PaintBoxShadowOuter and bug 514670
      if (innerRadii[C_TL].width > 0 || innerRadii[C_BL].width > 0) {
        borderSizes.left = spreadDistance;
      }

      if (innerRadii[C_TL].height > 0 || innerRadii[C_TR].height > 0) {
        borderSizes.top = spreadDistance;
      }

      if (innerRadii[C_TR].width > 0 || innerRadii[C_BR].width > 0) {
        borderSizes.right = spreadDistance;
      }

      if (innerRadii[C_BL].height > 0 || innerRadii[C_BR].height > 0) {
        borderSizes.bottom = spreadDistance;
      }

      nsCSSBorderRenderer::ComputeInnerRadii(innerRadii, borderSizes,
                                             &clipRectRadii);
    }

    // Set the "skip rect" to the area within the frame that we don't paint in,
    // including after blurring.
    nsRect skipRect = shadowClipRect;
    skipRect.Deflate(blurMargin);
    gfxRect skipGfxRect = nsLayoutUtils::RectToGfxRect(skipRect, oneDevPixel);
    if (hasBorderRadius) {
      skipGfxRect.Deflate(gfxMargin(
          std::max(clipRectRadii[C_TL].height, clipRectRadii[C_TR].height), 0,
          std::max(clipRectRadii[C_BL].height, clipRectRadii[C_BR].height), 0));
    }

    // When there's a blur radius, gfxGaussianBlur leaves the skiprect area
    // unchanged. And by construction the gfxSkipRect is not touched by the
    // rendered shadow (even after blurring), so those pixels must be completely
    // transparent in the shadow, so drawing them changes nothing.
    DrawTarget* drawTarget = aRenderingContext.GetDrawTarget();

    // Clip the context to the area of the frame's padding rect, so no part of
    // the shadow is painted outside. Also cut out anything beyond where the
    // inset shadow will be.
    Rect shadowGfxRect = NSRectToRect(paddingRect, oneDevPixel);
    shadowGfxRect.Round();

    sRGBColor shadowColor = GetShadowColor(shadow.base, aForFrame, 1.0);
    aRenderingContext.Save();

    // This clips the outside border radius.
    // clipRectRadii is the border radius inside the inset shadow.
    if (hasBorderRadius) {
      RefPtr<Path> roundedRect =
          MakePathForRoundedRect(*drawTarget, shadowGfxRect, innerRadii);
      aRenderingContext.Clip(roundedRect);
    } else {
      aRenderingContext.Clip(shadowGfxRect);
    }

    nsContextBoxBlur insetBoxBlur;
    gfxRect destRect =
        nsLayoutUtils::RectToGfxRect(shadowPaintRect, oneDevPixel);
    Point shadowOffset(shadow.base.horizontal.ToAppUnits() / oneDevPixel,
                       shadow.base.vertical.ToAppUnits() / oneDevPixel);

    insetBoxBlur.InsetBoxBlur(
        &aRenderingContext, ToRect(destRect), shadowClipGfxRect, shadowColor,
        blurRadius, spreadDistanceAppUnits, oneDevPixel, hasBorderRadius,
        clipRectRadii, ToRect(skipGfxRect), shadowOffset);
    aRenderingContext.Restore();
  }
}

/* static */
nsCSSRendering::PaintBGParams nsCSSRendering::PaintBGParams::ForAllLayers(
    nsPresContext& aPresCtx, const nsRect& aDirtyRect,
    const nsRect& aBorderArea, nsIFrame* aFrame, uint32_t aPaintFlags,
    float aOpacity) {
  MOZ_ASSERT(aFrame);

  PaintBGParams result(aPresCtx, aDirtyRect, aBorderArea, aFrame, aPaintFlags,
                       -1, CompositionOp::OP_OVER, aOpacity);

  return result;
}

/* static */
nsCSSRendering::PaintBGParams nsCSSRendering::PaintBGParams::ForSingleLayer(
    nsPresContext& aPresCtx, const nsRect& aDirtyRect,
    const nsRect& aBorderArea, nsIFrame* aFrame, uint32_t aPaintFlags,
    int32_t aLayer, CompositionOp aCompositionOp, float aOpacity) {
  MOZ_ASSERT(aFrame && (aLayer != -1));

  PaintBGParams result(aPresCtx, aDirtyRect, aBorderArea, aFrame, aPaintFlags,
                       aLayer, aCompositionOp, aOpacity);

  return result;
}

ImgDrawResult nsCSSRendering::PaintStyleImageLayer(const PaintBGParams& aParams,
                                                   gfxContext& aRenderingCtx) {
  AUTO_PROFILER_LABEL("nsCSSRendering::PaintStyleImageLayer", GRAPHICS);

  MOZ_ASSERT(aParams.frame,
             "Frame is expected to be provided to PaintStyleImageLayer");

  const ComputedStyle* sc = FindBackground(aParams.frame);
  if (!sc) {
    // We don't want to bail out if moz-appearance is set on a root
    // node. If it has a parent content node, bail because it's not
    // a root, otherwise keep going in order to let the theme stuff
    // draw the background. The canvas really should be drawing the
    // bg, but there's no way to hook that up via css.
    if (!aParams.frame->StyleDisplay()->HasNativeAppearance()) {
      return ImgDrawResult::SUCCESS;
    }

    nsIContent* content = aParams.frame->GetContent();
    if (!content || content->GetParent()) {
      return ImgDrawResult::SUCCESS;
    }

    sc = aParams.frame->Style();
  }

  return PaintStyleImageLayerWithSC(aParams, aRenderingCtx, sc,
                                    *aParams.frame->StyleBorder());
}

bool nsCSSRendering::CanBuildWebRenderDisplayItemsForStyleImageLayer(
    WebRenderLayerManager* aManager, nsPresContext& aPresCtx, nsIFrame* aFrame,
    const nsStyleBackground* aBackgroundStyle, int32_t aLayer,
    uint32_t aPaintFlags) {
  MOZ_ASSERT(aBackgroundStyle);
  MOZ_ASSERT(aFrame);
  MOZ_ASSERT(aLayer >= 0 &&
             (uint32_t)aLayer < aBackgroundStyle->mImage.mLayers.Length());

  const auto& styleImage =
      aBackgroundStyle->mImage.mLayers[aLayer].mImage.FinalImage();
  switch (styleImage.tag) {
    case StyleImage::Tag::ImageSet:
    case StyleImage::Tag::LightDark:
      MOZ_FALLTHROUGH_ASSERT("Should've been resolved");
    case StyleImage::Tag::Url:
    case StyleImage::Tag::Gradient:
    case StyleImage::Tag::MozSymbolicIcon:
    case StyleImage::Tag::Image:
    case StyleImage::Tag::None:
    case StyleImage::Tag::CrossFade:
      return true;
    case StyleImage::Tag::Element:
      // Try to not add to this branch.
      break;
  }
  return false;
}

ImgDrawResult nsCSSRendering::BuildWebRenderDisplayItemsForStyleImageLayer(
    const PaintBGParams& aParams, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager, nsDisplayItem* aItem) {
  MOZ_ASSERT(aParams.frame,
             "Frame is expected to be provided to "
             "BuildWebRenderDisplayItemsForStyleImageLayer");

  ComputedStyle* sc = FindBackground(aParams.frame);
  if (!sc) {
    // We don't want to bail out if moz-appearance is set on a root
    // node. If it has a parent content node, bail because it's not
    // a root, otherwise keep going in order to let the theme stuff
    // draw the background. The canvas really should be drawing the
    // bg, but there's no way to hook that up via css.
    if (!aParams.frame->StyleDisplay()->HasNativeAppearance()) {
      return ImgDrawResult::SUCCESS;
    }

    nsIContent* content = aParams.frame->GetContent();
    if (!content || content->GetParent()) {
      return ImgDrawResult::SUCCESS;
    }

    sc = aParams.frame->Style();
  }
  return BuildWebRenderDisplayItemsForStyleImageLayerWithSC(
      aParams, aBuilder, aResources, aSc, aManager, aItem, sc,
      *aParams.frame->StyleBorder());
}

static bool IsOpaqueBorderEdge(const nsStyleBorder& aBorder,
                               mozilla::Side aSide, const nsIFrame* aForFrame) {
  if (aBorder.GetComputedBorder().Side(aSide) == 0) {
    return true;
  }
  switch (aBorder.GetBorderStyle(aSide)) {
    case StyleBorderStyle::Solid:
    case StyleBorderStyle::Groove:
    case StyleBorderStyle::Ridge:
    case StyleBorderStyle::Inset:
    case StyleBorderStyle::Outset:
      break;
    default:
      return false;
  }

  // If we're using a border image, assume it's not fully opaque,
  // because we may not even have the image loaded at this point, and
  // even if we did, checking whether the relevant tile is fully
  // opaque would be too much work.
  if (!aBorder.mBorderImageSource.IsNone()) {
    return false;
  }
  return NS_GET_A(aBorder.BorderColorFor(aSide).CalcColor(aForFrame)) == 255;
}

/**
 * Returns true if all border edges are either missing or opaque.
 */
static bool IsOpaqueBorder(const nsStyleBorder& aBorder,
                           const nsIFrame* aForFrame) {
  for (const auto i : mozilla::AllPhysicalSides()) {
    if (!IsOpaqueBorderEdge(aBorder, i, aForFrame)) {
      return false;
    }
  }
  return true;
}

static inline void SetupDirtyRects(const nsRect& aBGClipArea,
                                   const nsRect& aCallerDirtyRect,
                                   nscoord aAppUnitsPerPixel,
                                   /* OUT: */
                                   nsRect* aDirtyRect, gfxRect* aDirtyRectGfx) {
  aDirtyRect->IntersectRect(aBGClipArea, aCallerDirtyRect);

  // Compute the Thebes equivalent of the dirtyRect.
  *aDirtyRectGfx = nsLayoutUtils::RectToGfxRect(*aDirtyRect, aAppUnitsPerPixel);
  NS_WARNING_ASSERTION(aDirtyRect->IsEmpty() || !aDirtyRectGfx->IsEmpty(),
                       "converted dirty rect should not be empty");
  MOZ_ASSERT(!aDirtyRect->IsEmpty() || aDirtyRectGfx->IsEmpty(),
             "second should be empty if first is");
}

static bool IsSVGStyleGeometryBox(StyleGeometryBox aBox) {
  return (aBox == StyleGeometryBox::FillBox ||
          aBox == StyleGeometryBox::StrokeBox ||
          aBox == StyleGeometryBox::ViewBox);
}

static StyleGeometryBox ComputeBoxValueForOrigin(
    nsIFrame* aForFrame, StyleBackgroundOrigin aOrigin) {
  // The mapping for mask-origin is from
  // https://drafts.fxtf.org/css-masking/#the-mask-origin
  const bool svgLayout = aForFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT);
  switch (aOrigin) {
    // For SVG elements without associated CSS layout box, the values
    // content-box, padding-box, border-box compute to fill-box.
    case StyleBackgroundOrigin::ContentBox:
      return svgLayout ? StyleGeometryBox::FillBox
                       : StyleGeometryBox::ContentBox;
    case StyleBackgroundOrigin::PaddingBox:
      return svgLayout ? StyleGeometryBox::FillBox
                       : StyleGeometryBox::PaddingBox;
    case StyleBackgroundOrigin::BorderBox:
      return svgLayout ? StyleGeometryBox::FillBox
                       : StyleGeometryBox::BorderBox;
    // For elements with associated CSS layout box, the values fill-box,
    // stroke-box and view-box compute to the initial value of mask-origin.
    case StyleBackgroundOrigin::FillBox:
      return svgLayout ? StyleGeometryBox::FillBox
                       : StyleGeometryBox::BorderBox;
    case StyleBackgroundOrigin::StrokeBox:
      return svgLayout ? StyleGeometryBox::StrokeBox
                       : StyleGeometryBox::BorderBox;
    case StyleBackgroundOrigin::ViewBox:
      return svgLayout ? StyleGeometryBox::ViewBox
                       : StyleGeometryBox::BorderBox;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown background-origin value");
  return StyleGeometryBox::BorderBox;
}

// Resolves a background-clip/mask-clip value into the geometry box it paints
// against. The mapping for mask-clip is from
// https://drafts.fxtf.org/css-masking/#the-mask-clip
static StyleGeometryBox ComputeBoxValueForClip(const nsIFrame* aForFrame,
                                               StyleBackgroundClip aClip) {
  // For SVG elements without an associated CSS layout box, content-box and
  // padding-box compute to fill-box and border-box computes to stroke-box.
  // For elements with an associated CSS layout box, fill-box computes to
  // content-box and stroke-box and view-box compute to border-box.
  const bool svg = aForFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT);
  switch (aClip) {
    case StyleBackgroundClip::ContentBox:
    case StyleBackgroundClip::FillBox:
      return svg ? StyleGeometryBox::FillBox : StyleGeometryBox::ContentBox;
    case StyleBackgroundClip::PaddingBox:
      return svg ? StyleGeometryBox::FillBox : StyleGeometryBox::PaddingBox;
    case StyleBackgroundClip::BorderBox:
    case StyleBackgroundClip::StrokeBox:
      return svg ? StyleGeometryBox::StrokeBox : StyleGeometryBox::BorderBox;
    case StyleBackgroundClip::ViewBox:
      return svg ? StyleGeometryBox::ViewBox : StyleGeometryBox::BorderBox;
    case StyleBackgroundClip::NoClip:
      return StyleGeometryBox::NoClip;
    case StyleBackgroundClip::Text:
      return StyleGeometryBox::Text;
    case StyleBackgroundClip::BorderArea:
      return StyleGeometryBox::BorderArea;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown background-clip/mask-clip value");
  return StyleGeometryBox::BorderBox;
}

bool nsCSSRendering::ImageLayerClipState::IsValid() const {
  // mDirtyRectInDevPx comes from mDirtyRectInAppUnits. mDirtyRectInAppUnits
  // can not be empty if mDirtyRectInDevPx is not.
  if (!mDirtyRectInDevPx.IsEmpty() && mDirtyRectInAppUnits.IsEmpty()) {
    return false;
  }

  if (mHasRoundedCorners == mClippedRadii.IsEmpty()) {
    return false;
  }

  return true;
}

/* static */
void nsCSSRendering::GetImageLayerClip(
    const nsStyleImageLayers::Layer& aLayer, nsIFrame* aForFrame,
    const nsStyleBorder& aBorder, const nsRect& aBorderArea,
    const nsRect& aCallerDirtyRect, bool aWillPaintBorder,
    nscoord aAppUnitsPerPixel,
    /* out */ ImageLayerClipState* aClipState) {
  StyleGeometryBox layerClip = ComputeBoxValueForClip(aForFrame, aLayer.mClip);
  if (IsSVGStyleGeometryBox(layerClip)) {
    MOZ_ASSERT(aForFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT));

    // The coordinate space of clipArea is svg user space.
    nsRect clipArea =
        nsLayoutUtils::ComputeSVGReferenceRect(aForFrame, layerClip);

    nsRect strokeBox = (layerClip == StyleGeometryBox::StrokeBox)
                           ? clipArea
                           : nsLayoutUtils::ComputeSVGReferenceRect(
                                 aForFrame, StyleGeometryBox::StrokeBox);
    nsRect clipAreaRelativeToStrokeBox = clipArea - strokeBox.TopLeft();

    // aBorderArea is the stroke-box area in a coordinate space defined by
    // the caller. This coordinate space can be svg user space of aForFrame,
    // the space of aForFrame's reference-frame, or anything else.
    //
    // Which coordinate space chosen for aBorderArea is not matter. What
    // matter is to ensure returning aClipState->mBGClipArea in the consistent
    // coordiante space with aBorderArea. So we evaluate the position of clip
    // area base on the position of aBorderArea here.
    aClipState->mBGClipArea =
        clipAreaRelativeToStrokeBox + aBorderArea.TopLeft();

    SetupDirtyRects(aClipState->mBGClipArea, aCallerDirtyRect,
                    aAppUnitsPerPixel, &aClipState->mDirtyRectInAppUnits,
                    &aClipState->mDirtyRectInDevPx);
    MOZ_ASSERT(aClipState->IsValid());
    return;
  }

  if (layerClip == StyleGeometryBox::NoClip) {
    aClipState->mBGClipArea = aCallerDirtyRect;

    SetupDirtyRects(aClipState->mBGClipArea, aCallerDirtyRect,
                    aAppUnitsPerPixel, &aClipState->mDirtyRectInAppUnits,
                    &aClipState->mDirtyRectInDevPx);
    MOZ_ASSERT(aClipState->IsValid());
    return;
  }

  MOZ_ASSERT(!aForFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT));

  // Compute the outermost boundary of the area that might be painted.
  // Same coordinate space as aBorderArea.
  Sides skipSides = aForFrame->GetSkipSides();
  nsRect clipBorderArea =
      BoxDecorationRectForBorder(aForFrame, aBorderArea, skipSides, &aBorder);

  bool haveRoundedCorners = false;
  LayoutFrameType fType = aForFrame->Type();
  if (fType != LayoutFrameType::TableColGroup &&
      fType != LayoutFrameType::TableCol &&
      fType != LayoutFrameType::TableRow &&
      fType != LayoutFrameType::TableRowGroup) {
    haveRoundedCorners = GetRadii(aForFrame, aBorder, aBorderArea,
                                  clipBorderArea, aClipState->mRadii);
  }
  const bool isSolidBorder =
      aWillPaintBorder && IsOpaqueBorder(aBorder, aForFrame);
  if (isSolidBorder && layerClip == StyleGeometryBox::BorderBox) {
    // If we have rounded corners, we need to inflate the background
    // drawing area a bit to avoid seams between the border and
    // background.
    layerClip = haveRoundedCorners ? StyleGeometryBox::MozAlmostPadding
                                   : StyleGeometryBox::PaddingBox;
  }

  aClipState->mBGClipArea = clipBorderArea;

  if (aForFrame->IsScrollContainerFrame() &&
      StyleImageLayerAttachment::Local == aLayer.mAttachment) {
    // As of this writing, this is still in discussion in the CSS Working Group
    // http://lists.w3.org/Archives/Public/www-style/2013Jul/0250.html

    // The rectangle for 'background-clip' scrolls with the content,
    // but the background is also clipped at a non-scrolling 'padding-box'
    // like the content. (See below.)
    // Therefore, only 'content-box' makes a difference here.
    if (layerClip == StyleGeometryBox::ContentBox) {
      ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(aForFrame);
      // Clip at a rectangle attached to the scrolled content.
      aClipState->mHasAdditionalBGClipArea = true;
      aClipState->mAdditionalBGClipArea =
          nsRect(aClipState->mBGClipArea.TopLeft() +
                     scrollContainerFrame->GetScrolledFrame()->GetPosition()
                     // For the dir=rtl case:
                     + scrollContainerFrame->GetScrollRange().TopLeft(),
                 scrollContainerFrame->GetScrolledRect().Size());
      nsMargin padding = aForFrame->GetUsedPadding();
      // padding-bottom is ignored on scrollable frames:
      // https://bugzilla.mozilla.org/show_bug.cgi?id=748518
      padding.bottom = 0;
      padding.ApplySkipSides(skipSides);
      aClipState->mAdditionalBGClipArea.Deflate(padding);
    }

    // Also clip at a non-scrolling, rounded-corner 'padding-box',
    // same as the scrolled content because of the 'overflow' property.
    layerClip = StyleGeometryBox::PaddingBox;
  }

  // See the comment of StyleGeometryBox::Margin.
  // Hitting this assertion means we decide to turn on margin-box support for
  // positioned mask from CSS parser and style system. In this case, you
  // should *inflate* mBGClipArea by the margin returning from
  // aForFrame->GetUsedMargin() in the code chunk bellow.
  MOZ_ASSERT(layerClip != StyleGeometryBox::MarginBox,
             "StyleGeometryBox::MarginBox rendering is not supported yet.\n");

  // 'border-area' uses the border box (like 'border-box') for the outer clip;
  // the padding box is then clipped out by the background display item itself
  // (see PushBorderAreaClipOut / ClipBackgroundToBorderArea).
  if (layerClip != StyleGeometryBox::BorderBox &&
      layerClip != StyleGeometryBox::Text &&
      layerClip != StyleGeometryBox::BorderArea) {
    nsMargin border = aForFrame->GetUsedBorder();
    if (layerClip == StyleGeometryBox::MozAlmostPadding) {
      // Reduce |border| by 1px (device pixels) on all sides, if
      // possible, so that we don't get antialiasing seams between the
      // {background|mask} and border.
      border.top = std::max(0, border.top - aAppUnitsPerPixel);
      border.right = std::max(0, border.right - aAppUnitsPerPixel);
      border.bottom = std::max(0, border.bottom - aAppUnitsPerPixel);
      border.left = std::max(0, border.left - aAppUnitsPerPixel);
    } else if (layerClip != StyleGeometryBox::PaddingBox) {
      NS_ASSERTION(layerClip == StyleGeometryBox::ContentBox,
                   "unexpected background-clip");
      border += aForFrame->GetUsedPadding();
    }
    border.ApplySkipSides(skipSides);
    aClipState->mBGClipArea.Deflate(border);

    if (haveRoundedCorners) {
      aClipState->mRadii.AdjustInwards(border);
    }
  }

  if (haveRoundedCorners) {
    auto d2a = aForFrame->PresContext()->AppUnitsPerDevPixel();
    nsCSSRendering::ComputePixelRadii(aClipState->mRadii, d2a,
                                      &aClipState->mClippedRadii);
    aClipState->mHasRoundedCorners = !aClipState->mClippedRadii.IsEmpty();
  }

  if (!haveRoundedCorners && aClipState->mHasAdditionalBGClipArea) {
    // Do the intersection here to account for the fast path(?) below.
    aClipState->mBGClipArea =
        aClipState->mBGClipArea.Intersect(aClipState->mAdditionalBGClipArea);
    aClipState->mHasAdditionalBGClipArea = false;
  }

  SetupDirtyRects(aClipState->mBGClipArea, aCallerDirtyRect, aAppUnitsPerPixel,
                  &aClipState->mDirtyRectInAppUnits,
                  &aClipState->mDirtyRectInDevPx);

  MOZ_ASSERT(aClipState->IsValid());
}

static void SetupImageLayerClip(nsCSSRendering::ImageLayerClipState& aClipState,
                                gfxContext* aCtx, nscoord aAppUnitsPerPixel,
                                gfxContextAutoSaveRestore* aAutoSR) {
  if (aClipState.mDirtyRectInDevPx.IsEmpty()) {
    // Our caller won't draw anything under this condition, so no need
    // to set more up.
    return;
  }

  if (aClipState.mCustomClip) {
    // We don't support custom clips and rounded corners, arguably a bug, but
    // table painting seems to depend on it.
    return;
  }

  // If we have rounded corners, clip all subsequent drawing to the
  // rounded rectangle defined by bgArea and bgRadii (we don't know
  // whether the rounded corners intrude on the dirtyRect or not).
  // Do not do this if we have a caller-provided clip rect --
  // as above with bgArea, arguably a bug, but table painting seems
  // to depend on it.

  if (aClipState.mHasAdditionalBGClipArea) {
    gfxRect bgAreaGfx = nsLayoutUtils::RectToGfxRect(
        aClipState.mAdditionalBGClipArea, aAppUnitsPerPixel);
    bgAreaGfx.Round();
    gfxUtils::ConditionRect(bgAreaGfx);

    aAutoSR->EnsureSaved(aCtx);
    aCtx->SnappedClip(bgAreaGfx);
  }

  if (aClipState.mHasRoundedCorners) {
    Rect bgAreaGfx = NSRectToRect(aClipState.mBGClipArea, aAppUnitsPerPixel);
    bgAreaGfx.Round();

    if (bgAreaGfx.IsEmpty()) {
      // I think it's become possible to hit this since
      // https://hg.mozilla.org/mozilla-central/rev/50e934e4979b landed.
      NS_WARNING("converted background area should not be empty");
      // Make our caller not do anything.
      aClipState.mDirtyRectInDevPx.SizeTo(gfxSize(0.0, 0.0));
      return;
    }

    aAutoSR->EnsureSaved(aCtx);

    RefPtr<Path> roundedRect = MakePathForRoundedRect(
        *aCtx->GetDrawTarget(), bgAreaGfx, aClipState.mClippedRadii);
    aCtx->Clip(roundedRect);
  }
}

static void DrawBackgroundColor(nsCSSRendering::ImageLayerClipState& aClipState,
                                gfxContext* aCtx, nscoord aAppUnitsPerPixel) {
  if (aClipState.mDirtyRectInDevPx.IsEmpty()) {
    // Our caller won't draw anything under this condition, so no need
    // to set more up.
    return;
  }

  DrawTarget* drawTarget = aCtx->GetDrawTarget();

  // We don't support custom clips and rounded corners, arguably a bug, but
  // table painting seems to depend on it.
  if (!aClipState.mHasRoundedCorners || aClipState.mCustomClip) {
    aCtx->NewPath();
    aCtx->SnappedRectangle(aClipState.mDirtyRectInDevPx);
    aCtx->Fill();
    return;
  }

  Rect bgAreaGfx = NSRectToRect(aClipState.mBGClipArea, aAppUnitsPerPixel);
  bgAreaGfx.Round();

  if (bgAreaGfx.IsEmpty()) {
    // I think it's become possible to hit this since
    // https://hg.mozilla.org/mozilla-central/rev/50e934e4979b landed.
    NS_WARNING("converted background area should not be empty");
    // Make our caller not do anything.
    aClipState.mDirtyRectInDevPx.SizeTo(gfxSize(0.0, 0.0));
    return;
  }

  aCtx->Save();
  gfxRect dirty = ThebesRect(bgAreaGfx).Intersect(aClipState.mDirtyRectInDevPx);

  aCtx->SnappedClip(dirty);

  if (aClipState.mHasAdditionalBGClipArea) {
    gfxRect bgAdditionalAreaGfx = nsLayoutUtils::RectToGfxRect(
        aClipState.mAdditionalBGClipArea, aAppUnitsPerPixel);
    bgAdditionalAreaGfx.Round();
    gfxUtils::ConditionRect(bgAdditionalAreaGfx);
    aCtx->SnappedClip(bgAdditionalAreaGfx);
  }

  RefPtr<Path> roundedRect =
      MakePathForRoundedRect(*drawTarget, bgAreaGfx, aClipState.mClippedRadii);
  aCtx->SetPath(roundedRect);
  aCtx->Fill();
  aCtx->Restore();
}

enum class ScrollbarColorKind {
  Thumb,
  Track,
};

static Maybe<nscolor> CalcScrollbarColor(nsIFrame* aFrame,
                                         ScrollbarColorKind aKind) {
  ComputedStyle* scrollbarStyle = nsLayoutUtils::StyleForScrollbar(aFrame);
  const auto& colors = scrollbarStyle->StyleUI()->mScrollbarColor;
  if (colors.IsAuto()) {
    return Nothing();
  }
  const auto& color = aKind == ScrollbarColorKind::Thumb
                          ? colors.AsColors().thumb
                          : colors.AsColors().track;
  return Some(color.CalcColor(*scrollbarStyle));
}

static nscolor GetBackgroundColor(nsIFrame* aFrame,
                                  const ComputedStyle* aStyle) {
  switch (aStyle->StyleDisplay()->EffectiveAppearance()) {
    case StyleAppearance::ScrollbarthumbVertical:
    case StyleAppearance::ScrollbarthumbHorizontal: {
      if (Maybe<nscolor> overrideColor =
              CalcScrollbarColor(aFrame, ScrollbarColorKind::Thumb)) {
        return *overrideColor;
      }
      break;
    }
    case StyleAppearance::ScrollbarVertical:
    case StyleAppearance::ScrollbarHorizontal:
    case StyleAppearance::Scrollcorner: {
      if (Maybe<nscolor> overrideColor =
              CalcScrollbarColor(aFrame, ScrollbarColorKind::Track)) {
        return *overrideColor;
      }
      break;
    }
    default:
      break;
  }
  return aStyle->GetVisitedDependentColor(&nsStyleBackground::mBackgroundColor);
}

nscolor nsCSSRendering::DetermineBackgroundColor(nsPresContext* aPresContext,
                                                 const ComputedStyle* aStyle,
                                                 nsIFrame* aFrame,
                                                 bool& aDrawBackgroundImage,
                                                 bool& aDrawBackgroundColor) {
  auto shouldPaint = aFrame->ComputeShouldPaintBackground();
  aDrawBackgroundImage = shouldPaint.mImage;
  aDrawBackgroundColor = shouldPaint.mColor;

  const nsStyleBackground* bg = aStyle->StyleBackground();
  nscolor bgColor;
  if (aDrawBackgroundColor) {
    bgColor = GetBackgroundColor(aFrame, aStyle);
    if (NS_GET_A(bgColor) == 0) {
      aDrawBackgroundColor = false;
    }
  } else {
    // If GetBackgroundColorDraw() is false, we are still expected to
    // draw color in the background of any frame that's not completely
    // transparent, but we are expected to use white instead of whatever
    // color was specified.
    bgColor = NS_RGB(255, 255, 255);
    if (aDrawBackgroundImage || !bg->IsTransparent(aStyle)) {
      aDrawBackgroundColor = true;
    } else {
      bgColor = NS_RGBA(0, 0, 0, 0);
    }
  }

  // We can skip painting the background color if a background image is opaque.
  nsStyleImageLayers::Repeat repeat = bg->BottomLayer().mRepeat;
  bool xFullRepeat = repeat.mXRepeat == StyleImageLayerRepeat::Repeat ||
                     repeat.mXRepeat == StyleImageLayerRepeat::Round;
  bool yFullRepeat = repeat.mYRepeat == StyleImageLayerRepeat::Repeat ||
                     repeat.mYRepeat == StyleImageLayerRepeat::Round;
  if (aDrawBackgroundColor && xFullRepeat && yFullRepeat &&
      bg->BottomLayer().mImage.IsOpaque() &&
      bg->BottomLayer().mBlendMode == StyleBlend::Normal) {
    aDrawBackgroundColor = false;
  }

  return bgColor;
}

static CompositionOp DetermineCompositionOp(
    const nsCSSRendering::PaintBGParams& aParams,
    const nsStyleImageLayers& aLayers, uint32_t aLayerIndex) {
  if (aParams.layer >= 0) {
    // When drawing a single layer, use the specified composition op.
    return aParams.compositionOp;
  }

  const nsStyleImageLayers::Layer& layer = aLayers.mLayers[aLayerIndex];
  // When drawing all layers, get the compositon op from each image layer.
  if (aParams.paintFlags & nsCSSRendering::PAINTBG_MASK_IMAGE) {
    // Always using OP_OVER mode while drawing the bottom mask layer.
    if (aLayerIndex == (aLayers.mImageCount - 1)) {
      return CompositionOp::OP_OVER;
    }

    return nsCSSRendering::GetGFXCompositeMode(layer.mComposite);
  }

  return nsCSSRendering::GetGFXBlendMode(layer.mBlendMode);
}

ImgDrawResult nsCSSRendering::PaintStyleImageLayerWithSC(
    const PaintBGParams& aParams, gfxContext& aRenderingCtx,
    const ComputedStyle* aBackgroundSC, const nsStyleBorder& aBorder) {
  MOZ_ASSERT(aParams.frame,
             "Frame is expected to be provided to PaintStyleImageLayerWithSC");

  // If we're drawing all layers, aCompositonOp is ignored, so make sure that
  // it was left at its default value.
  MOZ_ASSERT(aParams.layer != -1 ||
             aParams.compositionOp == CompositionOp::OP_OVER);

  // Check to see if we have an appearance defined.  If so, we let the theme
  // renderer draw the background and bail out.
  // XXXzw this ignores aParams.bgClipRect.
  StyleAppearance appearance =
      aParams.frame->StyleDisplay()->EffectiveAppearance();
  if (appearance != StyleAppearance::None) {
    nsITheme* theme = aParams.presCtx.Theme();
    if (theme->ThemeSupportsWidget(&aParams.presCtx, aParams.frame,
                                   appearance)) {
      nsRect drawing(aParams.borderArea);
      theme->GetWidgetOverflow(aParams.presCtx.DeviceContext(), aParams.frame,
                               appearance, &drawing);
      drawing.IntersectRect(drawing, aParams.dirtyRect);
      theme->DrawWidgetBackground(&aRenderingCtx, aParams.frame, appearance,
                                  aParams.borderArea, drawing);
      return ImgDrawResult::SUCCESS;
    }
  }

  // For canvas frames (in the CSS sense) we draw the background color using
  // a solid color item that gets added in nsLayoutUtils::PaintFrame, or
  // nsSubDocumentFrame::BuildDisplayList (bug 488242).  Either way we don't
  // need to paint the background color here.
  bool isCanvasFrame = aParams.frame->IsCanvasFrame();
  const bool paintMask = aParams.paintFlags & PAINTBG_MASK_IMAGE;

  // Determine whether we are drawing background images and/or
  // background colors.
  bool drawBackgroundImage = true;
  bool drawBackgroundColor = !paintMask;
  nscolor bgColor = NS_RGBA(0, 0, 0, 0);
  if (!paintMask) {
    bgColor =
        DetermineBackgroundColor(&aParams.presCtx, aBackgroundSC, aParams.frame,
                                 drawBackgroundImage, drawBackgroundColor);
  }

  // Masks shouldn't be suppressed for print.
  MOZ_ASSERT_IF(paintMask, drawBackgroundImage);

  const nsStyleImageLayers& layers =
      paintMask ? aBackgroundSC->StyleSVGReset()->mMask
                : aBackgroundSC->StyleBackground()->mImage;
  // If we're drawing a specific layer, we don't want to draw the
  // background color.
  if (drawBackgroundColor && aParams.layer >= 0) {
    drawBackgroundColor = false;
  }

  // At this point, drawBackgroundImage and drawBackgroundColor are
  // true if and only if we are actually supposed to paint an image or
  // color into aDirtyRect, respectively.
  if (!drawBackgroundImage && !drawBackgroundColor) {
    return ImgDrawResult::SUCCESS;
  }

  // The 'bgClipArea' (used only by the image tiling logic, far below)
  // is the caller-provided aParams.bgClipRect if any, or else the area
  // determined by the value of 'background-clip' in
  // SetupCurrentBackgroundClip.  (Arguably it should be the
  // intersection, but that breaks the table painter -- in particular,
  // taking the intersection breaks reftests/bugs/403249-1[ab].)
  nscoord appUnitsPerPixel = aParams.presCtx.AppUnitsPerDevPixel();
  ImageLayerClipState clipState;
  if (aParams.bgClipRect) {
    clipState.mBGClipArea = *aParams.bgClipRect;
    clipState.mCustomClip = true;
    clipState.mHasRoundedCorners = false;
    SetupDirtyRects(clipState.mBGClipArea, aParams.dirtyRect, appUnitsPerPixel,
                    &clipState.mDirtyRectInAppUnits,
                    &clipState.mDirtyRectInDevPx);
  } else {
    GetImageLayerClip(layers.BottomLayer(), aParams.frame, aBorder,
                      aParams.borderArea, aParams.dirtyRect,
                      (aParams.paintFlags & PAINTBG_WILL_PAINT_BORDER),
                      appUnitsPerPixel, &clipState);
  }

  // If we might be using a background color, go ahead and set it now.
  if (drawBackgroundColor && !isCanvasFrame) {
    aRenderingCtx.SetColor(sRGBColor::FromABGR(bgColor));
  }

  // If there is no background image, draw a color.  (If there is
  // neither a background image nor a color, we wouldn't have gotten
  // this far.)
  if (!drawBackgroundImage) {
    if (!isCanvasFrame) {
      DrawBackgroundColor(clipState, &aRenderingCtx, appUnitsPerPixel);
    }
    return ImgDrawResult::SUCCESS;
  }

  if (layers.mImageCount < 1) {
    // Return if there are no background layers, all work from this point
    // onwards happens iteratively on these.
    return ImgDrawResult::SUCCESS;
  }

  MOZ_ASSERT((aParams.layer < 0) ||
             (layers.mImageCount > uint32_t(aParams.layer)));

  // The background color is rendered over the entire dirty area,
  // even if the image isn't.
  if (drawBackgroundColor && !isCanvasFrame) {
    DrawBackgroundColor(clipState, &aRenderingCtx, appUnitsPerPixel);
  }

  // Compute the outermost boundary of the area that might be painted.
  // Same coordinate space as aParams.borderArea & aParams.bgClipRect.
  Sides skipSides = aParams.frame->GetSkipSides();
  nsRect paintBorderArea = BoxDecorationRectForBackground(
      aParams.frame, aParams.borderArea, skipSides, &aBorder);
  nsRect clipBorderArea = BoxDecorationRectForBorder(
      aParams.frame, aParams.borderArea, skipSides, &aBorder);

  ImgDrawResult result = ImgDrawResult::SUCCESS;
  StyleBackgroundClip currentBackgroundClip = StyleBackgroundClip::BorderBox;
  const bool drawAllLayers = (aParams.layer < 0);
  uint32_t count = drawAllLayers
                       ? layers.mImageCount  // iterate all image layers.
                       : layers.mImageCount -
                             aParams.layer;  // iterate from the bottom layer to
                                             // the 'aParams.layer-th' layer.
  NS_FOR_VISIBLE_IMAGE_LAYERS_BACK_TO_FRONT_WITH_RANGE(
      i, layers, layers.mImageCount - 1, count) {
    // NOTE: no Save() yet, we do that later by calling autoSR.EnsureSaved(ctx)
    // in the cases we need it.
    gfxContextAutoSaveRestore autoSR;
    const nsStyleImageLayers::Layer& layer = layers.mLayers[i];

    ImageLayerClipState currentLayerClipState = clipState;
    if (!aParams.bgClipRect) {
      bool isBottomLayer = (i == layers.mImageCount - 1);
      if (currentBackgroundClip != layer.mClip || isBottomLayer) {
        currentBackgroundClip = layer.mClip;
        if (!isBottomLayer) {
          currentLayerClipState = {};
          // For the bottom layer, we already called GetImageLayerClip above
          // and it stored its results in clipState.
          GetImageLayerClip(layer, aParams.frame, aBorder, aParams.borderArea,
                            aParams.dirtyRect,
                            (aParams.paintFlags & PAINTBG_WILL_PAINT_BORDER),
                            appUnitsPerPixel, &currentLayerClipState);
        }
        SetupImageLayerClip(currentLayerClipState, &aRenderingCtx,
                            appUnitsPerPixel, &autoSR);
        if (!clipBorderArea.IsEqualEdges(aParams.borderArea)) {
          // We're drawing the background for the joined continuation boxes
          // so we need to clip that to the slice that we want for this
          // frame.
          gfxRect clip = nsLayoutUtils::RectToGfxRect(aParams.borderArea,
                                                      appUnitsPerPixel);
          autoSR.EnsureSaved(&aRenderingCtx);
          aRenderingCtx.SnappedClip(clip);
        }
      }
    }

    // Skip the following layer preparing and painting code if the current
    // layer is not selected for drawing.
    if (aParams.layer >= 0 && i != (uint32_t)aParams.layer) {
      continue;
    }
    nsBackgroundLayerState state = PrepareImageLayer(
        &aParams.presCtx, aParams.frame, aParams.paintFlags, paintBorderArea,
        currentLayerClipState.mBGClipArea, layer, nullptr);
    result &= state.mImageRenderer.PrepareResult();

    // Skip the layer painting code if we found the dirty region is empty.
    if (currentLayerClipState.mDirtyRectInDevPx.IsEmpty()) {
      continue;
    }

    if (!state.mFillArea.IsEmpty()) {
      CompositionOp co = DetermineCompositionOp(aParams, layers, i);
      if (co != CompositionOp::OP_OVER) {
        NS_ASSERTION(aRenderingCtx.CurrentOp() == CompositionOp::OP_OVER,
                     "It is assumed the initial op is OP_OVER, when it is "
                     "restored later");
        aRenderingCtx.SetOp(co);
      }

      result &= state.mImageRenderer.DrawLayer(
          &aParams.presCtx, aRenderingCtx, state.mDestArea, state.mFillArea,
          state.mAnchor + paintBorderArea.TopLeft(),
          currentLayerClipState.mDirtyRectInAppUnits, state.mRepeatSize,
          aParams.opacity);

      if (co != CompositionOp::OP_OVER) {
        aRenderingCtx.SetOp(CompositionOp::OP_OVER);
      }
    }
  }

  return result;
}

ImgDrawResult
nsCSSRendering::BuildWebRenderDisplayItemsForStyleImageLayerWithSC(
    const PaintBGParams& aParams, mozilla::wr::DisplayListBuilder& aBuilder,
    mozilla::wr::IpcResourceUpdateQueue& aResources,
    const mozilla::layers::StackingContextHelper& aSc,
    mozilla::layers::RenderRootStateManager* aManager, nsDisplayItem* aItem,
    ComputedStyle* aBackgroundSC, const nsStyleBorder& aBorder) {
  MOZ_ASSERT(!(aParams.paintFlags & PAINTBG_MASK_IMAGE));

  nscoord appUnitsPerPixel = aParams.presCtx.AppUnitsPerDevPixel();
  ImageLayerClipState clipState;

  clipState.mBGClipArea = *aParams.bgClipRect;
  clipState.mCustomClip = true;
  clipState.mHasRoundedCorners = false;
  SetupDirtyRects(clipState.mBGClipArea, aParams.dirtyRect, appUnitsPerPixel,
                  &clipState.mDirtyRectInAppUnits,
                  &clipState.mDirtyRectInDevPx);

  // Compute the outermost boundary of the area that might be painted.
  // Same coordinate space as aParams.borderArea & aParams.bgClipRect.
  Sides skipSides = aParams.frame->GetSkipSides();
  nsRect paintBorderArea = BoxDecorationRectForBackground(
      aParams.frame, aParams.borderArea, skipSides, &aBorder);

  const nsStyleImageLayers& layers = aBackgroundSC->StyleBackground()->mImage;
  const nsStyleImageLayers::Layer& layer = layers.mLayers[aParams.layer];

  // Skip the following layer painting code if we found the dirty region is
  // empty or the current layer is not selected for drawing.
  if (clipState.mDirtyRectInDevPx.IsEmpty()) {
    return ImgDrawResult::SUCCESS;
  }

  ImgDrawResult result = ImgDrawResult::SUCCESS;
  nsBackgroundLayerState state =
      PrepareImageLayer(&aParams.presCtx, aParams.frame, aParams.paintFlags,
                        paintBorderArea, clipState.mBGClipArea, layer, nullptr);
  result &= state.mImageRenderer.PrepareResult();

  if (!state.mFillArea.IsEmpty()) {
    result &= state.mImageRenderer.BuildWebRenderDisplayItemsForLayer(
        &aParams.presCtx, aBuilder, aResources, aSc, aManager, aItem,
        state.mDestArea, state.mFillArea,
        state.mAnchor + paintBorderArea.TopLeft(),
        clipState.mDirtyRectInAppUnits, state.mRepeatSize, aParams.opacity);
  }

  return result;
}

nsRect nsCSSRendering::ComputeImageLayerPositioningArea(
    nsPresContext* aPresContext, nsIFrame* aForFrame, const nsRect& aBorderArea,
    const nsStyleImageLayers::Layer& aLayer, nsIFrame** aAttachedToFrame,
    bool* aOutIsTransformedFixed) {
  // Compute {background|mask} origin area relative to aBorderArea now as we
  // may need  it to compute the effective image size for a CSS gradient.
  nsRect positionArea;

  StyleGeometryBox layerOrigin =
      ComputeBoxValueForOrigin(aForFrame, aLayer.mOrigin);

  if (IsSVGStyleGeometryBox(layerOrigin)) {
    MOZ_ASSERT(aForFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT));
    *aAttachedToFrame = aForFrame;

    positionArea =
        nsLayoutUtils::ComputeSVGReferenceRect(aForFrame, layerOrigin);

    nsPoint toStrokeBoxOffset = nsPoint(0, 0);
    if (layerOrigin != StyleGeometryBox::StrokeBox) {
      nsRect strokeBox = nsLayoutUtils::ComputeSVGReferenceRect(
          aForFrame, StyleGeometryBox::StrokeBox);
      toStrokeBoxOffset = positionArea.TopLeft() - strokeBox.TopLeft();
    }

    // For SVG frames, the return value is relative to the stroke box
    return nsRect(toStrokeBoxOffset, positionArea.Size());
  }

  MOZ_ASSERT(!aForFrame->HasAnyStateBits(NS_FRAME_SVG_LAYOUT));

  LayoutFrameType frameType = aForFrame->Type();
  nsIFrame* geometryFrame = aForFrame;
  if (MOZ_UNLIKELY(frameType == LayoutFrameType::ScrollContainer &&
                   StyleImageLayerAttachment::Local == aLayer.mAttachment)) {
    ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(aForFrame);
    positionArea =
        nsRect(scrollContainerFrame->GetScrolledFrame()->GetPosition()
                   // For the dir=rtl case:
                   + scrollContainerFrame->GetScrollRange().TopLeft(),
               scrollContainerFrame->GetScrolledRect().Size());
    // The ScrolledRect’s size does not include the borders or scrollbars,
    // reverse the handling of background-origin
    // compared to the common case below.
    if (layerOrigin == StyleGeometryBox::BorderBox) {
      nsMargin border = geometryFrame->GetUsedBorder();
      border.ApplySkipSides(geometryFrame->GetSkipSides());
      positionArea.Inflate(border);
      positionArea.Inflate(scrollContainerFrame->GetActualScrollbarSizes());
    } else if (layerOrigin != StyleGeometryBox::PaddingBox) {
      nsMargin padding = geometryFrame->GetUsedPadding();
      padding.ApplySkipSides(geometryFrame->GetSkipSides());
      positionArea.Deflate(padding);
      NS_ASSERTION(layerOrigin == StyleGeometryBox::ContentBox,
                   "unknown background-origin value");
    }
    *aAttachedToFrame = aForFrame;
    return positionArea;
  }

  if (MOZ_UNLIKELY(frameType == LayoutFrameType::Canvas)) {
    geometryFrame = aForFrame->PrincipalChildList().FirstChild();
    // geometryFrame might be null if this canvas is a page created
    // as an overflow container (e.g. the in-flow content has already
    // finished and this page only displays the continuations of
    // absolutely positioned content).
    if (geometryFrame) {
      positionArea =
          nsPlaceholderFrame::GetRealFrameFor(geometryFrame)->GetRect();
    }
  } else {
    positionArea = nsRect(nsPoint(0, 0), aBorderArea.Size());
  }

  // {background|mask} images are tiled over the '{background|mask}-clip' area
  // but the origin of the tiling is based on the '{background|mask}-origin'
  // area.
  if (layerOrigin != StyleGeometryBox::BorderBox && geometryFrame) {
    nsMargin border = geometryFrame->GetUsedBorder();
    if (layerOrigin != StyleGeometryBox::PaddingBox) {
      border += geometryFrame->GetUsedPadding();
      NS_ASSERTION(layerOrigin == StyleGeometryBox::ContentBox,
                   "unknown background-origin value");
    }
    positionArea.Deflate(border);
  }

  nsIFrame* attachedToFrame = aForFrame;
  if (StyleImageLayerAttachment::Fixed == aLayer.mAttachment) {
    // If it's a fixed background attachment, then the image is placed
    // relative to the viewport, which is the area of the root frame
    // in a screen context or the page content frame in a print context.
    attachedToFrame = aPresContext->PresShell()->GetRootFrame();
    NS_ASSERTION(attachedToFrame, "no root frame");
    nsIFrame* pageContentFrame = nullptr;
    if (aPresContext->IsPaginated()) {
      pageContentFrame = nsLayoutUtils::GetClosestFrameOfType(
          aForFrame, LayoutFrameType::PageContent);
      if (pageContentFrame) {
        attachedToFrame = pageContentFrame;
      }
      // else this is an embedded shell and its root frame is what we want
    }

    // If the background is affected by a transform, treat is as if it
    // wasn't fixed.
    if (nsLayoutUtils::IsTransformed(aForFrame, attachedToFrame)) {
      attachedToFrame = aForFrame;
      *aOutIsTransformedFixed = true;
    } else {
      // Set the background positioning area to the viewport's area
      // (relative to aForFrame)
      positionArea = nsRect(-aForFrame->GetOffsetTo(attachedToFrame),
                            attachedToFrame->GetSize());

      if (!pageContentFrame) {
        // Subtract the size of scrollbars.
        if (ScrollContainerFrame* sf =
                aPresContext->PresShell()->GetRootScrollContainerFrame()) {
          nsMargin scrollbars = sf->GetActualScrollbarSizes();
          positionArea.Deflate(scrollbars);
        }
      }

      // If we have the dynamic toolbar, we need to expand the image area to
      // include the region under the dynamic toolbar, otherwise we will see a
      // blank space under the toolbar.
      if (aPresContext->IsRootContentDocumentCrossProcess() &&
          aPresContext->HasDynamicToolbar()) {
        positionArea.SizeTo(nsLayoutUtils::ExpandHeightForDynamicToolbar(
            aPresContext, positionArea.Size()));
      }
    }
  }
  *aAttachedToFrame = attachedToFrame;

  return positionArea;
}

/* static */
nscoord nsCSSRendering::ComputeRoundedSize(nscoord aCurrentSize,
                                           nscoord aPositioningSize) {
  float repeatCount = NS_roundf(float(aPositioningSize) / float(aCurrentSize));
  if (repeatCount < 1.0f) {
    return aPositioningSize;
  }
  return nscoord(NS_lround(float(aPositioningSize) / repeatCount));
}

// Apply the CSS image sizing algorithm as it applies to background images.
// See http://www.w3.org/TR/css3-background/#the-background-size .
// aIntrinsicSize is the size that the background image 'would like to be'.
// It can be found by calling nsImageRenderer::ComputeIntrinsicSize.
static nsSize ComputeDrawnSizeForBackground(
    const CSSSizeOrRatio& aIntrinsicSize, const nsSize& aBgPositioningArea,
    const StyleBackgroundSize& aLayerSize, StyleImageLayerRepeat aXRepeat,
    StyleImageLayerRepeat aYRepeat) {
  nsSize imageSize;

  // Size is dictated by cover or contain rules.
  if (aLayerSize.IsContain() || aLayerSize.IsCover()) {
    nsImageRenderer::FitType fitType = aLayerSize.IsCover()
                                           ? nsImageRenderer::COVER
                                           : nsImageRenderer::CONTAIN;
    imageSize = nsImageRenderer::ComputeConstrainedSize(
        aBgPositioningArea, aIntrinsicSize.mRatio, fitType);
  } else {
    MOZ_ASSERT(aLayerSize.IsExplicitSize());
    const auto& width = aLayerSize.explicit_size.width;
    const auto& height = aLayerSize.explicit_size.height;
    // No cover/contain constraint, use default algorithm.
    CSSSizeOrRatio specifiedSize;
    if (width.IsLengthPercentage()) {
      specifiedSize.SetWidth(
          width.AsLengthPercentage().Resolve(aBgPositioningArea.width));
    }
    if (height.IsLengthPercentage()) {
      specifiedSize.SetHeight(
          height.AsLengthPercentage().Resolve(aBgPositioningArea.height));
    }

    imageSize = nsImageRenderer::ComputeConcreteSize(
        specifiedSize, aIntrinsicSize, aBgPositioningArea);
  }

  // See https://www.w3.org/TR/css3-background/#background-size .
  // "If 'background-repeat' is 'round' for one (or both) dimensions, there is a
  // second
  //  step. The UA must scale the image in that dimension (or both dimensions)
  //  so that it fits a whole number of times in the background positioning
  //  area."
  // "If 'background-repeat' is 'round' for one dimension only and if
  // 'background-size'
  //  is 'auto' for the other dimension, then there is a third step: that other
  //  dimension is scaled so that the original aspect ratio is restored."
  bool isRepeatRoundInBothDimensions =
      aXRepeat == StyleImageLayerRepeat::Round &&
      aYRepeat == StyleImageLayerRepeat::Round;

  // Calculate the rounded size only if the background-size computation
  // returned a correct size for the image.
  if (imageSize.width && aXRepeat == StyleImageLayerRepeat::Round) {
    imageSize.width = nsCSSRendering::ComputeRoundedSize(
        imageSize.width, aBgPositioningArea.width);
    if (!isRepeatRoundInBothDimensions && aLayerSize.IsExplicitSize() &&
        aLayerSize.explicit_size.height.IsAuto()) {
      // Restore intrinsic ratio
      if (aIntrinsicSize.mRatio) {
        imageSize.height =
            aIntrinsicSize.mRatio.Inverted().ApplyTo(imageSize.width);
      }
    }
  }

  // Calculate the rounded size only if the background-size computation
  // returned a correct size for the image.
  if (imageSize.height && aYRepeat == StyleImageLayerRepeat::Round) {
    imageSize.height = nsCSSRendering::ComputeRoundedSize(
        imageSize.height, aBgPositioningArea.height);
    if (!isRepeatRoundInBothDimensions && aLayerSize.IsExplicitSize() &&
        aLayerSize.explicit_size.width.IsAuto()) {
      // Restore intrinsic ratio
      if (aIntrinsicSize.mRatio) {
        imageSize.width = aIntrinsicSize.mRatio.ApplyTo(imageSize.height);
      }
    }
  }

  return imageSize;
}

auto nsCSSRendering::ComputeBackgroundImageInfo(
    const CSSSizeOrRatio& aIntrinsicSize, const nsSize& aPositionAreaSize,
    const nsStyleImageLayers::Layer& aLayer) -> BackgroundImageInfo {
  BackgroundImageInfo result;

  // Scale the image as specified for background-size and background-repeat.
  // Also as required for proper background positioning when background-position
  // is defined with percentages.
  result.mSize = ComputeDrawnSizeForBackground(
      aIntrinsicSize, aPositionAreaSize, aLayer.mSize, aLayer.mRepeat.mXRepeat,
      aLayer.mRepeat.mYRepeat);

  if (result.mSize.IsEmpty()) {
    return result;
  }

  // Compute the anchor point, relative to the positioning area's top left
  // (which is where the top-left of aForFrame's border-box will be rendered).
  // Also compute the position of the background now that the background's size
  // is determined.
  nsImageRenderer::ComputeObjectAnchorPoint(aLayer.mPosition, aPositionAreaSize,
                                            result.mSize, &result.mTopLeft,
                                            &result.mAnchor);
  return result;
}

/* ComputeSpacedRepeatSize
 * aImageDimension: the image width/height
 * aAvailableSpace: the background positioning area width/height
 * aRepeat: determine whether the image is repeated
 * Returns the image size plus gap size of app units for use as spacing
 */
static nscoord ComputeSpacedRepeatSize(nscoord aImageDimension,
                                       nscoord aAvailableSpace, bool& aRepeat) {
  float ratio = static_cast<float>(aAvailableSpace) / aImageDimension;

  if (ratio < 2.0f) {  // If you can't repeat at least twice, then don't repeat.
    aRepeat = false;
    return aImageDimension;
  }

  aRepeat = true;
  return (aAvailableSpace - aImageDimension) / (NSToIntFloor(ratio) - 1);
}

/* static */
nscoord nsCSSRendering::ComputeBorderSpacedRepeatSize(nscoord aImageDimension,
                                                      nscoord aAvailableSpace,
                                                      nscoord& aSpace) {
  int32_t count = aImageDimension ? (aAvailableSpace / aImageDimension) : 0;
  aSpace = (aAvailableSpace - aImageDimension * count) / (count + 1);
  return aSpace + aImageDimension;
}

nsBackgroundLayerState nsCSSRendering::PrepareImageLayer(
    nsPresContext* aPresContext, nsIFrame* aForFrame, uint32_t aFlags,
    const nsRect& aBorderArea, const nsRect& aBGClipRect,
    const nsStyleImageLayers::Layer& aLayer, bool* aOutIsTransformedFixed) {
  /*
   * The properties we need to keep in mind when drawing style image
   * layers are:
   *
   *   background-image/ mask-image
   *   background-repeat/ mask-repeat
   *   background-attachment
   *   background-position/ mask-position
   *   background-clip/ mask-clip
   *   background-origin/ mask-origin
   *   background-size/ mask-size
   *   background-blend-mode
   *   box-decoration-break
   *   mask-mode
   *   mask-composite
   *
   * (background-color applies to the entire element and not to individual
   * layers, so it is irrelevant to this method.)
   *
   * These properties have the following dependencies upon each other when
   * determining rendering:
   *
   *   background-image/ mask-image
   *     no dependencies
   *   background-repeat/ mask-repeat
   *     no dependencies
   *   background-attachment
   *     no dependencies
   *   background-position/ mask-position
   *     depends upon background-size/mask-size (for the image's scaled size)
   *     and background-break (for the background positioning area)
   *   background-clip/ mask-clip
   *     no dependencies
   *   background-origin/ mask-origin
   *     depends upon background-attachment (only in the case where that value
   *     is 'fixed')
   *   background-size/ mask-size
   *     depends upon box-decoration-break (for the background positioning area
   *     for resolving percentages), background-image (for the image's intrinsic
   *     size), background-repeat (if that value is 'round'), and
   *     background-origin (for the background painting area, when
   *     background-repeat is 'round')
   *   background-blend-mode
   *     no dependencies
   *   mask-mode
   *     no dependencies
   *   mask-composite
   *     no dependencies
   *   box-decoration-break
   *     no dependencies
   *
   * As a result of only-if dependencies we don't strictly do a topological
   * sort of the above properties when processing, but it's pretty close to one:
   *
   *   background-clip/mask-clip (by caller)
   *   background-image/ mask-image
   *   box-decoration-break, background-origin/ mask origin
   *   background-attachment (postfix for background-origin if 'fixed')
   *   background-size/ mask-size
   *   background-position/ mask-position
   *   background-repeat/ mask-repeat
   */

  uint32_t irFlags = 0;
  if (aFlags & nsCSSRendering::PAINTBG_SYNC_DECODE_IMAGES) {
    irFlags |= nsImageRenderer::FLAG_SYNC_DECODE_IMAGES;
  }
  if (aFlags & nsCSSRendering::PAINTBG_TO_WINDOW) {
    irFlags |= nsImageRenderer::FLAG_PAINTING_TO_WINDOW;
  }
  if (aFlags & nsCSSRendering::PAINTBG_HIGH_QUALITY_SCALING) {
    irFlags |= nsImageRenderer::FLAG_HIGH_QUALITY_SCALING;
  }
  // Only do partial bg image drawing in content documents: non-content
  // documents are viewed as UI of the browser and a partial draw of a bg image
  // might look weird in that context.
  if (XRE_IsContentProcess() && !aPresContext->IsChrome()) {
    irFlags |= nsImageRenderer::FLAG_DRAW_PARTIAL_FRAMES;
  }

  nsBackgroundLayerState state(aForFrame, &aLayer.mImage, irFlags);
  if (!state.mImageRenderer.PrepareImage(Some(nsImageRenderer::PositionParams{
          aLayer,
          aBorderArea,
          aBGClipRect,
      }))) {
    // There's no image or it's not ready to be painted.
    if (aOutIsTransformedFixed &&
        StyleImageLayerAttachment::Fixed == aLayer.mAttachment) {
      nsIFrame* attachedToFrame = aPresContext->PresShell()->GetRootFrame();
      NS_ASSERTION(attachedToFrame, "no root frame");
      nsIFrame* pageContentFrame = nullptr;
      if (aPresContext->IsPaginated()) {
        pageContentFrame = nsLayoutUtils::GetClosestFrameOfType(
            aForFrame, LayoutFrameType::PageContent);
        if (pageContentFrame) {
          attachedToFrame = pageContentFrame;
        }
        // else this is an embedded shell and its root frame is what we want
      }

      *aOutIsTransformedFixed =
          nsLayoutUtils::IsTransformed(aForFrame, attachedToFrame);
    }
    return state;
  }

  // The frame to which the background is attached
  nsIFrame* attachedToFrame = aForFrame;
  // Is the background marked 'fixed', but affected by a transform?
  bool transformedFixed = false;
  // Compute background origin area relative to aBorderArea now as we may need
  // it to compute the effective image size for a CSS gradient.
  const auto positionArea = ComputeImageLayerPositioningArea(
      aPresContext, aForFrame, aBorderArea, aLayer, &attachedToFrame,
      &transformedFixed);
  if (aOutIsTransformedFixed) {
    *aOutIsTransformedFixed = transformedFixed;
  }

  // For background-attachment:fixed backgrounds, we'll override the area
  // where the background can be drawn to the viewport.
  nsRect bgClipRect = aBGClipRect;

  if (StyleImageLayerAttachment::Fixed == aLayer.mAttachment &&
      !transformedFixed && (aFlags & nsCSSRendering::PAINTBG_TO_WINDOW)) {
    bgClipRect = positionArea + aBorderArea.TopLeft();
  }

  auto repeatX = aLayer.mRepeat.mXRepeat;
  auto repeatY = aLayer.mRepeat.mYRepeat;

  const auto intrinsicSize = state.mImageRenderer.ComputeIntrinsicSize();
  const auto bgPositionSize = positionArea.Size();
  auto imageInfo =
      ComputeBackgroundImageInfo(intrinsicSize, bgPositionSize, aLayer);
  if (imageInfo.IsEmpty()) {
    return state;
  }

  state.mAnchor = imageInfo.mAnchor;
  state.mImageRenderer.SetPreferredSize(intrinsicSize, imageInfo.mSize);

  state.mRepeatSize = imageInfo.mSize;
  if (repeatX == StyleImageLayerRepeat::Space) {
    bool isRepeat;
    state.mRepeatSize.width = ComputeSpacedRepeatSize(
        imageInfo.mSize.width, bgPositionSize.width, isRepeat);
    if (isRepeat) {
      imageInfo.mTopLeft.x = 0;
      state.mAnchor.x = 0;
    } else {
      repeatX = StyleImageLayerRepeat::NoRepeat;
    }
  }

  if (repeatY == StyleImageLayerRepeat::Space) {
    bool isRepeat;
    state.mRepeatSize.height = ComputeSpacedRepeatSize(
        imageInfo.mSize.height, bgPositionSize.height, isRepeat);
    if (isRepeat) {
      imageInfo.mTopLeft.y = 0;
      state.mAnchor.y = 0;
    } else {
      repeatY = StyleImageLayerRepeat::NoRepeat;
    }
  }

  imageInfo.mTopLeft += positionArea.TopLeft();
  state.mAnchor += positionArea.TopLeft();
  state.mDestArea =
      nsRect(imageInfo.mTopLeft + aBorderArea.TopLeft(), imageInfo.mSize);
  state.mFillArea = state.mDestArea;

  ExtendMode repeatMode = ExtendMode::CLAMP;
  if (repeatX == StyleImageLayerRepeat::Repeat ||
      repeatX == StyleImageLayerRepeat::Round ||
      repeatX == StyleImageLayerRepeat::Space) {
    state.mFillArea.x = bgClipRect.x;
    state.mFillArea.width = bgClipRect.width;
    repeatMode = ExtendMode::REPEAT_X;
  }
  if (repeatY == StyleImageLayerRepeat::Repeat ||
      repeatY == StyleImageLayerRepeat::Round ||
      repeatY == StyleImageLayerRepeat::Space) {
    state.mFillArea.y = bgClipRect.y;
    state.mFillArea.height = bgClipRect.height;

    /***
     * We're repeating on the X axis already,
     * so if we have to repeat in the Y axis,
     * we really need to repeat in both directions.
     */
    if (repeatMode == ExtendMode::REPEAT_X) {
      repeatMode = ExtendMode::REPEAT;
    } else {
      repeatMode = ExtendMode::REPEAT_Y;
    }
  }
  state.mImageRenderer.SetExtendMode(repeatMode);
  state.mImageRenderer.SetMaskOp(aLayer.mMaskMode);

  state.mFillArea.IntersectRect(state.mFillArea, bgClipRect);

  return state;
}

nsRect nsCSSRendering::GetBackgroundLayerRect(
    nsPresContext* aPresContext, nsIFrame* aForFrame, const nsRect& aBorderArea,
    const nsRect& aClipRect, const nsStyleImageLayers::Layer& aLayer,
    uint32_t aFlags) {
  Sides skipSides = aForFrame->GetSkipSides();
  nsRect borderArea =
      BoxDecorationRectForBackground(aForFrame, aBorderArea, skipSides);
  nsBackgroundLayerState state = PrepareImageLayer(
      aPresContext, aForFrame, aFlags, borderArea, aClipRect, aLayer);
  return state.mFillArea;
}

// Begin table border-collapsing section
// These functions were written to not disrupt the normal ones and yet satisfy
// some additional requirements At some point, all functions should be unified
// to include the additional functionality that these provide

static nscoord RoundIntToPixel(nscoord aValue, nscoord aOneDevPixel,
                               bool aRoundDown = false) {
  if (aOneDevPixel <= 0) {
    // We must be rendering to a device that has a resolution greater than
    // one device pixel!
    // In that case, aValue is as accurate as it's going to get.
    return aValue;
  }

  nscoord halfPixel = NSToCoordRound(aOneDevPixel / 2.0f);
  nscoord extra = aValue % aOneDevPixel;
  nscoord finalValue = (!aRoundDown && (extra >= halfPixel))
                           ? aValue + (aOneDevPixel - extra)
                           : aValue - extra;
  return finalValue;
}

static nscoord RoundFloatToPixel(float aValue, nscoord aOneDevPixel,
                                 bool aRoundDown = false) {
  return RoundIntToPixel(NSToCoordRound(aValue), aOneDevPixel, aRoundDown);
}

static void SetPoly(const Rect& aRect, Point* poly) {
  poly[0].x = aRect.x;
  poly[0].y = aRect.y;
  poly[1].x = aRect.x + aRect.width;
  poly[1].y = aRect.y;
  poly[2].x = aRect.x + aRect.width;
  poly[2].y = aRect.y + aRect.height;
  poly[3].x = aRect.x;
  poly[3].y = aRect.y + aRect.height;
}

static void DrawDashedSegment(DrawTarget& aDrawTarget, nsRect aRect,
                              nscoord aDashLength, nscolor aColor,
                              int32_t aAppUnitsPerDevPixel, bool aHorizontal) {
  ColorPattern color(ToDeviceColor(aColor));
  DrawOptions drawOptions(1.f, CompositionOp::OP_OVER, AntialiasMode::NONE);
  StrokeOptions strokeOptions;

  Float dash[2];
  dash[0] = Float(aDashLength) / aAppUnitsPerDevPixel;
  dash[1] = dash[0];

  strokeOptions.mDashPattern = dash;
  strokeOptions.mDashLength = std::size(dash);

  if (aHorizontal) {
    nsPoint left = (aRect.TopLeft() + aRect.BottomLeft()) / 2;
    nsPoint right = (aRect.TopRight() + aRect.BottomRight()) / 2;
    strokeOptions.mLineWidth = Float(aRect.height) / aAppUnitsPerDevPixel;
    StrokeLineWithSnapping(left, right, aAppUnitsPerDevPixel, aDrawTarget,
                           color, strokeOptions, drawOptions);
  } else {
    nsPoint top = (aRect.TopLeft() + aRect.TopRight()) / 2;
    nsPoint bottom = (aRect.BottomLeft() + aRect.BottomRight()) / 2;
    strokeOptions.mLineWidth = Float(aRect.width) / aAppUnitsPerDevPixel;
    StrokeLineWithSnapping(top, bottom, aAppUnitsPerDevPixel, aDrawTarget,
                           color, strokeOptions, drawOptions);
  }
}

static void DrawSolidBorderSegment(
    DrawTarget& aDrawTarget, nsRect aRect, nscolor aColor,
    int32_t aAppUnitsPerDevPixel,
    mozilla::Side aStartBevelSide = mozilla::eSideTop,
    nscoord aStartBevelOffset = 0,
    mozilla::Side aEndBevelSide = mozilla::eSideTop,
    nscoord aEndBevelOffset = 0) {
  ColorPattern color(ToDeviceColor(aColor));
  DrawOptions drawOptions(1.f, CompositionOp::OP_OVER, AntialiasMode::NONE);

  nscoord oneDevPixel = NSIntPixelsToAppUnits(1, aAppUnitsPerDevPixel);
  // We don't need to bevel single pixel borders
  if ((aRect.width == oneDevPixel) || (aRect.height == oneDevPixel) ||
      ((0 == aStartBevelOffset) && (0 == aEndBevelOffset))) {
    // simple rectangle
    aDrawTarget.FillRect(
        NSRectToSnappedRect(aRect, aAppUnitsPerDevPixel, aDrawTarget), color,
        drawOptions);
  } else {
    // polygon with beveling
    Point poly[4];
    SetPoly(NSRectToSnappedRect(aRect, aAppUnitsPerDevPixel, aDrawTarget),
            poly);

    Float startBevelOffset =
        NSAppUnitsToFloatPixels(aStartBevelOffset, aAppUnitsPerDevPixel);
    switch (aStartBevelSide) {
      case eSideTop:
        poly[0].x += startBevelOffset;
        break;
      case eSideBottom:
        poly[3].x += startBevelOffset;
        break;
      case eSideRight:
        poly[1].y += startBevelOffset;
        break;
      case eSideLeft:
        poly[0].y += startBevelOffset;
    }

    Float endBevelOffset =
        NSAppUnitsToFloatPixels(aEndBevelOffset, aAppUnitsPerDevPixel);
    switch (aEndBevelSide) {
      case eSideTop:
        poly[1].x -= endBevelOffset;
        break;
      case eSideBottom:
        poly[2].x -= endBevelOffset;
        break;
      case eSideRight:
        poly[2].y -= endBevelOffset;
        break;
      case eSideLeft:
        poly[3].y -= endBevelOffset;
    }

    RefPtr<PathBuilder> builder = aDrawTarget.CreatePathBuilder();
    builder->MoveTo(poly[0]);
    builder->LineTo(poly[1]);
    builder->LineTo(poly[2]);
    builder->LineTo(poly[3]);
    builder->Close();
    RefPtr<Path> path = builder->Finish();
    aDrawTarget.Fill(path, color, drawOptions);
  }
}

static void GetDashInfo(nscoord aBorderLength, nscoord aDashLength,
                        nscoord aOneDevPixel, int32_t& aNumDashSpaces,
                        nscoord& aStartDashLength, nscoord& aEndDashLength) {
  aNumDashSpaces = 0;
  if (aStartDashLength + aDashLength + aEndDashLength >= aBorderLength) {
    aStartDashLength = aBorderLength;
    aEndDashLength = 0;
  } else {
    aNumDashSpaces =
        (aBorderLength - aDashLength) / (2 * aDashLength);  // round down
    nscoord extra = aBorderLength - aStartDashLength - aEndDashLength -
                    (((2 * aNumDashSpaces) - 1) * aDashLength);
    if (extra > 0) {
      nscoord half = RoundIntToPixel(extra / 2, aOneDevPixel);
      aStartDashLength += half;
      aEndDashLength += (extra - half);
    }
  }
}

void nsCSSRendering::DrawTableBorderSegment(
    DrawTarget& aDrawTarget, StyleBorderStyle aBorderStyle,
    nscolor aBorderColor, const nsRect& aBorder, int32_t aAppUnitsPerDevPixel,
    mozilla::Side aStartBevelSide, nscoord aStartBevelOffset,
    mozilla::Side aEndBevelSide, nscoord aEndBevelOffset) {
  bool horizontal =
      ((eSideTop == aStartBevelSide) || (eSideBottom == aStartBevelSide));
  nscoord oneDevPixel = NSIntPixelsToAppUnits(1, aAppUnitsPerDevPixel);

  if ((oneDevPixel >= aBorder.width) || (oneDevPixel >= aBorder.height) ||
      (StyleBorderStyle::Dashed == aBorderStyle) ||
      (StyleBorderStyle::Dotted == aBorderStyle)) {
    // no beveling for 1 pixel border, dash or dot
    aStartBevelOffset = 0;
    aEndBevelOffset = 0;
  }

  switch (aBorderStyle) {
    case StyleBorderStyle::None:
    case StyleBorderStyle::Hidden:
      // NS_ASSERTION(false, "style of none or hidden");
      break;
    case StyleBorderStyle::Dotted:
    case StyleBorderStyle::Dashed: {
      nscoord dashLength =
          (StyleBorderStyle::Dashed == aBorderStyle) ? DASH_LENGTH : DOT_LENGTH;
      // make the dash length proportional to the border thickness
      dashLength *= (horizontal) ? aBorder.height : aBorder.width;
      // make the min dash length for the ends 1/2 the dash length
      nscoord minDashLength =
          (StyleBorderStyle::Dashed == aBorderStyle)
              ? RoundFloatToPixel(((float)dashLength) / 2.0f,
                                  aAppUnitsPerDevPixel)
              : dashLength;
      minDashLength = std::max(minDashLength, oneDevPixel);
      nscoord numDashSpaces = 0;
      nscoord startDashLength = minDashLength;
      nscoord endDashLength = minDashLength;
      if (horizontal) {
        GetDashInfo(aBorder.width, dashLength, aAppUnitsPerDevPixel,
                    numDashSpaces, startDashLength, endDashLength);
        nsRect rect(aBorder.x, aBorder.y, startDashLength, aBorder.height);
        DrawSolidBorderSegment(aDrawTarget, rect, aBorderColor,
                               aAppUnitsPerDevPixel);

        rect.x += startDashLength + dashLength;
        rect.width =
            aBorder.width - (startDashLength + endDashLength + dashLength);
        DrawDashedSegment(aDrawTarget, rect, dashLength, aBorderColor,
                          aAppUnitsPerDevPixel, horizontal);

        rect.x += rect.width;
        rect.width = endDashLength;
        DrawSolidBorderSegment(aDrawTarget, rect, aBorderColor,
                               aAppUnitsPerDevPixel);
      } else {
        GetDashInfo(aBorder.height, dashLength, aAppUnitsPerDevPixel,
                    numDashSpaces, startDashLength, endDashLength);
        nsRect rect(aBorder.x, aBorder.y, aBorder.width, startDashLength);
        DrawSolidBorderSegment(aDrawTarget, rect, aBorderColor,
                               aAppUnitsPerDevPixel);

        rect.y += rect.height + dashLength;
        rect.height =
            aBorder.height - (startDashLength + endDashLength + dashLength);
        DrawDashedSegment(aDrawTarget, rect, dashLength, aBorderColor,
                          aAppUnitsPerDevPixel, horizontal);

        rect.y += rect.height;
        rect.height = endDashLength;
        DrawSolidBorderSegment(aDrawTarget, rect, aBorderColor,
                               aAppUnitsPerDevPixel);
      }
    } break;
    default:
      AutoTArray<SolidBeveledBorderSegment, 3> segments;
      GetTableBorderSolidSegments(
          segments, aBorderStyle, aBorderColor, aBorder, aAppUnitsPerDevPixel,
          aStartBevelSide, aStartBevelOffset, aEndBevelSide, aEndBevelOffset);
      for (const auto& segment : segments) {
        DrawSolidBorderSegment(
            aDrawTarget, segment.mRect, segment.mColor, aAppUnitsPerDevPixel,
            segment.mStartBevel.mSide, segment.mStartBevel.mOffset,
            segment.mEndBevel.mSide, segment.mEndBevel.mOffset);
      }
      break;
  }
}

void nsCSSRendering::GetTableBorderSolidSegments(
    nsTArray<SolidBeveledBorderSegment>& aSegments,
    StyleBorderStyle aBorderStyle, nscolor aBorderColor, const nsRect& aBorder,
    int32_t aAppUnitsPerDevPixel, mozilla::Side aStartBevelSide,
    nscoord aStartBevelOffset, mozilla::Side aEndBevelSide,
    nscoord aEndBevelOffset) {
  const bool horizontal =
      eSideTop == aStartBevelSide || eSideBottom == aStartBevelSide;
  const nscoord oneDevPixel = NSIntPixelsToAppUnits(1, aAppUnitsPerDevPixel);

  switch (aBorderStyle) {
    case StyleBorderStyle::None:
    case StyleBorderStyle::Hidden:
      return;
    case StyleBorderStyle::Dotted:
    case StyleBorderStyle::Dashed:
      MOZ_ASSERT_UNREACHABLE("Caller should have checked");
      return;
    case StyleBorderStyle::Groove:
    case StyleBorderStyle::Ridge:
      if ((horizontal && (oneDevPixel >= aBorder.height)) ||
          (!horizontal && (oneDevPixel >= aBorder.width))) {
        aSegments.AppendElement(
            SolidBeveledBorderSegment{aBorder,
                                      aBorderColor,
                                      {aStartBevelSide, aStartBevelOffset},
                                      {aEndBevelSide, aEndBevelOffset}});
      } else {
        nscoord startBevel =
            (aStartBevelOffset > 0)
                ? RoundFloatToPixel(0.5f * (float)aStartBevelOffset,
                                    aAppUnitsPerDevPixel, true)
                : 0;
        nscoord endBevel =
            (aEndBevelOffset > 0)
                ? RoundFloatToPixel(0.5f * (float)aEndBevelOffset,
                                    aAppUnitsPerDevPixel, true)
                : 0;
        mozilla::Side ridgeGrooveSide = (horizontal) ? eSideTop : eSideLeft;
        // FIXME: In theory, this should use the visited-dependent
        // background color, but I don't care.
        nscolor bevelColor =
            MakeBevelColor(ridgeGrooveSide, aBorderStyle, aBorderColor);
        nsRect rect(aBorder);
        nscoord half;
        if (horizontal) {  // top, bottom
          half = RoundFloatToPixel(0.5f * (float)aBorder.height,
                                   aAppUnitsPerDevPixel);
          rect.height = half;
          if (eSideTop == aStartBevelSide) {
            rect.x += startBevel;
            rect.width -= startBevel;
          }
          if (eSideTop == aEndBevelSide) {
            rect.width -= endBevel;
          }
          aSegments.AppendElement(
              SolidBeveledBorderSegment{rect,
                                        bevelColor,
                                        {aStartBevelSide, startBevel},
                                        {aEndBevelSide, endBevel}});
        } else {  // left, right
          half = RoundFloatToPixel(0.5f * (float)aBorder.width,
                                   aAppUnitsPerDevPixel);
          rect.width = half;
          if (eSideLeft == aStartBevelSide) {
            rect.y += startBevel;
            rect.height -= startBevel;
          }
          if (eSideLeft == aEndBevelSide) {
            rect.height -= endBevel;
          }
          aSegments.AppendElement(
              SolidBeveledBorderSegment{rect,
                                        bevelColor,
                                        {aStartBevelSide, startBevel},
                                        {aEndBevelSide, endBevel}});
        }

        rect = aBorder;
        ridgeGrooveSide =
            (eSideTop == ridgeGrooveSide) ? eSideBottom : eSideRight;
        // FIXME: In theory, this should use the visited-dependent
        // background color, but I don't care.
        bevelColor =
            MakeBevelColor(ridgeGrooveSide, aBorderStyle, aBorderColor);
        if (horizontal) {
          rect.y = rect.y + half;
          rect.height = aBorder.height - half;
          if (eSideBottom == aStartBevelSide) {
            rect.x += startBevel;
            rect.width -= startBevel;
          }
          if (eSideBottom == aEndBevelSide) {
            rect.width -= endBevel;
          }
          aSegments.AppendElement(
              SolidBeveledBorderSegment{rect,
                                        bevelColor,
                                        {aStartBevelSide, startBevel},
                                        {aEndBevelSide, endBevel}});
        } else {
          rect.x = rect.x + half;
          rect.width = aBorder.width - half;
          if (eSideRight == aStartBevelSide) {
            rect.y += aStartBevelOffset - startBevel;
            rect.height -= startBevel;
          }
          if (eSideRight == aEndBevelSide) {
            rect.height -= endBevel;
          }
          aSegments.AppendElement(
              SolidBeveledBorderSegment{rect,
                                        bevelColor,
                                        {aStartBevelSide, startBevel},
                                        {aEndBevelSide, endBevel}});
        }
      }
      break;
    case StyleBorderStyle::Double:
      // We can only do "double" borders if the thickness of the border
      // is more than 2px.  Otherwise, we fall through to painting a
      // solid border.
      if ((aBorder.width > 2 * oneDevPixel || horizontal) &&
          (aBorder.height > 2 * oneDevPixel || !horizontal)) {
        nscoord startBevel =
            (aStartBevelOffset > 0)
                ? RoundFloatToPixel(0.333333f * (float)aStartBevelOffset,
                                    aAppUnitsPerDevPixel)
                : 0;
        nscoord endBevel =
            (aEndBevelOffset > 0)
                ? RoundFloatToPixel(0.333333f * (float)aEndBevelOffset,
                                    aAppUnitsPerDevPixel)
                : 0;
        if (horizontal) {  // top, bottom
          nscoord thirdHeight = RoundFloatToPixel(
              0.333333f * (float)aBorder.height, aAppUnitsPerDevPixel);

          // draw the top line or rect
          nsRect topRect(aBorder.x, aBorder.y, aBorder.width, thirdHeight);
          if (eSideTop == aStartBevelSide) {
            topRect.x += aStartBevelOffset - startBevel;
            topRect.width -= aStartBevelOffset - startBevel;
          }
          if (eSideTop == aEndBevelSide) {
            topRect.width -= aEndBevelOffset - endBevel;
          }

          aSegments.AppendElement(
              SolidBeveledBorderSegment{topRect,
                                        aBorderColor,
                                        {aStartBevelSide, startBevel},
                                        {aEndBevelSide, endBevel}});

          // draw the botom line or rect
          nscoord heightOffset = aBorder.height - thirdHeight;
          nsRect bottomRect(aBorder.x, aBorder.y + heightOffset, aBorder.width,
                            aBorder.height - heightOffset);
          if (eSideBottom == aStartBevelSide) {
            bottomRect.x += aStartBevelOffset - startBevel;
            bottomRect.width -= aStartBevelOffset - startBevel;
          }
          if (eSideBottom == aEndBevelSide) {
            bottomRect.width -= aEndBevelOffset - endBevel;
          }
          aSegments.AppendElement(
              SolidBeveledBorderSegment{bottomRect,
                                        aBorderColor,
                                        {aStartBevelSide, startBevel},
                                        {aEndBevelSide, endBevel}});
        } else {  // left, right
          nscoord thirdWidth = RoundFloatToPixel(
              0.333333f * (float)aBorder.width, aAppUnitsPerDevPixel);

          nsRect leftRect(aBorder.x, aBorder.y, thirdWidth, aBorder.height);
          if (eSideLeft == aStartBevelSide) {
            leftRect.y += aStartBevelOffset - startBevel;
            leftRect.height -= aStartBevelOffset - startBevel;
          }
          if (eSideLeft == aEndBevelSide) {
            leftRect.height -= aEndBevelOffset - endBevel;
          }

          aSegments.AppendElement(
              SolidBeveledBorderSegment{leftRect,
                                        aBorderColor,
                                        {aStartBevelSide, startBevel},
                                        {aEndBevelSide, endBevel}});

          nscoord widthOffset = aBorder.width - thirdWidth;
          nsRect rightRect(aBorder.x + widthOffset, aBorder.y,
                           aBorder.width - widthOffset, aBorder.height);
          if (eSideRight == aStartBevelSide) {
            rightRect.y += aStartBevelOffset - startBevel;
            rightRect.height -= aStartBevelOffset - startBevel;
          }
          if (eSideRight == aEndBevelSide) {
            rightRect.height -= aEndBevelOffset - endBevel;
          }
          aSegments.AppendElement(
              SolidBeveledBorderSegment{rightRect,
                                        aBorderColor,
                                        {aStartBevelSide, startBevel},
                                        {aEndBevelSide, endBevel}});
        }
        break;
      }
      // else fall through to solid
      [[fallthrough]];
    case StyleBorderStyle::Solid:
      aSegments.AppendElement(
          SolidBeveledBorderSegment{aBorder,
                                    aBorderColor,
                                    {aStartBevelSide, aStartBevelOffset},
                                    {aEndBevelSide, aEndBevelOffset}});
      break;
    case StyleBorderStyle::Outset:
    case StyleBorderStyle::Inset:
      MOZ_ASSERT_UNREACHABLE(
          "inset, outset should have been converted to groove, ridge");
      break;
  }
}

// End table border-collapsing section

Rect nsCSSRendering::ExpandPaintingRectForDecorationLine(
    nsIFrame* aFrame, const StyleTextDecorationStyle aStyle,
    const Rect& aClippedRect, const Float aICoordInFrame,
    const Float aCycleLength, bool aVertical) {
  switch (aStyle) {
    case StyleTextDecorationStyle::Dotted:
    case StyleTextDecorationStyle::Dashed:
    case StyleTextDecorationStyle::Wavy:
      break;
    default:
      NS_ERROR("Invalid style was specified");
      return aClippedRect;
  }

  nsBlockFrame* block = nullptr;
  // Note that when we paint the decoration lines in relative positioned
  // box, we should paint them like all of the boxes are positioned as static.
  nscoord framePosInBlockAppUnits = 0;
  for (nsIFrame* f = aFrame; f; f = f->GetParent()) {
    block = do_QueryFrame(f);
    if (block) {
      break;
    }
    framePosInBlockAppUnits +=
        aVertical ? f->GetNormalPosition().y : f->GetNormalPosition().x;
  }

  NS_ENSURE_TRUE(block, aClippedRect);

  nsPresContext* pc = aFrame->PresContext();
  Float framePosInBlock =
      Float(pc->AppUnitsToGfxUnits(framePosInBlockAppUnits));
  int32_t rectPosInBlock = int32_t(NS_round(framePosInBlock + aICoordInFrame));
  int32_t extraStartEdge =
      rectPosInBlock - (rectPosInBlock / int32_t(aCycleLength) * aCycleLength);
  Rect rect(aClippedRect);
  if (aVertical) {
    rect.y -= extraStartEdge;
    rect.height += extraStartEdge;
  } else {
    rect.x -= extraStartEdge;
    rect.width += extraStartEdge;
  }
  return rect;
}

// Converts a GfxFont to an SkFont
// Either returns true if it was successful, or false if something went wrong
static bool GetSkFontFromGfxFont(DrawTarget& aDrawTarget, gfxFont* aFont,
                                 SkFont& aSkFont) {
  RefPtr<ScaledFont> scaledFont = aFont->GetScaledFont(&aDrawTarget);
  if (!scaledFont) {
    return false;
  }

  ScaledFontBase* fontBase = static_cast<ScaledFontBase*>(scaledFont.get());

  SkTypeface* typeface = fontBase->GetSkTypeface();
  if (!typeface) {
    return false;
  }

  aSkFont = SkFont(sk_ref_sp(typeface), SkFloatToScalar(fontBase->GetSize()));
  return true;
}

// Computes data used to position the decoration line within a
// SkTextBlob, data is returned through aBounds
static void GetPositioning(
    const nsCSSRendering::PaintDecorationLineParams& aParams, const Rect& aRect,
    Float aOneCSSPixel, Float aCenterBaselineOffset, SkScalar aBounds[]) {
  /**
   * How Positioning in Skia Works
   *  Take the letter "n" for example
   *  We set textPos as 0, 0
   *  This is represented in Skia like so (not to scale)
   *        ^
   *  -10px |  _ __
   *        | | '_ \
   *   -5px | | | | |
   * y-axis | |_| |_|
   *  (0,0) ----------------------->
   *        |     5px        10px
   *    5px |
   *        |
   *   10px |
   *        v
   *  0 on the x axis is a line that touches the bottom of the n
   *  (0,0) is the bottom left-hand corner of the n character
   *  Moving "up" from the n is going in a negative y direction
   *  Moving "down" from the n is going in a positive y direction
   *
   *  The intercepts that are returned in this arrangement will be
   *  offset by the original point it starts at. (This happens in
   *  the SkipInk function below).
   *
   *  In Skia, text MUST be laid out such that the next character
   *  in the RunBuffer is further along the x-axis than the previous
   *  character, otherwise there is undefined/strange behavior.
   */

  Float rectThickness = aParams.vertical ? aRect.Width() : aRect.Height();

  // the upper and lower lines/edges of the under or over line
  SkScalar upperLine, lowerLine;
  if (aParams.decoration == mozilla::StyleTextDecorationLine::OVERLINE) {
    lowerLine =
        -aParams.offset + aParams.defaultLineThickness - aCenterBaselineOffset;
    upperLine = lowerLine - rectThickness;
  } else {
    // underlines in vertical text are offset from the center of
    // the text, and not the baseline
    // Skia sets the text at it's baseline so we have to offset it
    // for text in vertical-* writing modes
    upperLine = -aParams.offset - aCenterBaselineOffset;
    lowerLine = upperLine + rectThickness;
  }

  // set up the bounds, add in a little padding to the thickness of the line
  // (unless the line is <= 1 CSS pixel thick)
  Float lineThicknessPadding = aParams.lineSize.height > aOneCSSPixel
                                   ? 0.25f * aParams.lineSize.height
                                   : 0;
  // don't allow padding greater than 0.75 CSS pixel
  lineThicknessPadding = std::min(lineThicknessPadding, 0.75f * aOneCSSPixel);
  aBounds[0] = upperLine - lineThicknessPadding;
  aBounds[1] = lowerLine + lineThicknessPadding;
}

// positions an individual glyph according to the given offset
static SkPoint GlyphPosition(const gfxTextRun::DetailedGlyph& aGlyph,
                             const SkPoint& aTextPos,
                             int32_t aAppUnitsPerDevPixel) {
  SkPoint point = {aGlyph.mOffset.x, aGlyph.mOffset.y};

  // convert to device pixels
  point.fX /= (float)aAppUnitsPerDevPixel;
  point.fY /= (float)aAppUnitsPerDevPixel;

  // add offsets
  point.fX += aTextPos.fX;
  point.fY += aTextPos.fY;
  return point;
}

// returns a count of all the glyphs that will be rendered
// excludes ligature continuations, includes the number of individual
// glyph records. This includes the number of DetailedGlyphs that a single
// CompressedGlyph record points to. This function is necessary because Skia
// needs the total length of glyphs to add to it's run buffer before it creates
// the RunBuffer object, and this cannot be resized later.
static uint32_t CountAllGlyphs(
    const gfxTextRun* aTextRun,
    const gfxTextRun::CompressedGlyph* aCompressedGlyph, uint32_t aStringStart,
    uint32_t aStringEnd) {
  uint32_t totalGlyphCount = 0;

  for (const gfxTextRun::CompressedGlyph* cg = aCompressedGlyph + aStringStart;
       cg < aCompressedGlyph + aStringEnd; ++cg) {
    totalGlyphCount += cg->IsSimpleGlyph() ? 1 : cg->GetGlyphCount();
  }

  return totalGlyphCount;
}

static void AddDetailedGlyph(const SkTextBlobBuilder::RunBuffer& aRunBuffer,
                             const gfxTextRun::DetailedGlyph& aGlyph,
                             int aIndex, float aAppUnitsPerDevPixel,
                             SkPoint& aTextPos) {
  // add glyph ID to the run buffer at i
  aRunBuffer.glyphs[aIndex] = aGlyph.mGlyphID;

  // position the glyph correctly using the detailed offsets
  SkPoint position = GlyphPosition(aGlyph, aTextPos, aAppUnitsPerDevPixel);
  aRunBuffer.pos[2 * aIndex] = position.fX;
  aRunBuffer.pos[(2 * aIndex) + 1] = position.fY;

  // increase aTextPos.fx by the advance
  aTextPos.fX += ((float)aGlyph.mAdvance / aAppUnitsPerDevPixel);
}

static void AddSimpleGlyph(const SkTextBlobBuilder::RunBuffer& aRunBuffer,
                           const gfxTextRun::CompressedGlyph& aGlyph,
                           int aIndex, float aAppUnitsPerDevPixel,
                           SkPoint& aTextPos) {
  aRunBuffer.glyphs[aIndex] = aGlyph.GetSimpleGlyph();

  // simple glyphs are offset from 0, so we'll just use textPos
  aRunBuffer.pos[2 * aIndex] = aTextPos.fX;
  aRunBuffer.pos[(2 * aIndex) + 1] = aTextPos.fY;

  // increase aTextPos.fX by the advance
  aTextPos.fX += ((float)aGlyph.GetSimpleAdvance() / aAppUnitsPerDevPixel);
}

// Sets up a Skia TextBlob of the specified font, text position, and made up of
// the glyphs between aStringStart and aStringEnd. Handles RTL and LTR text
// and positions each glyph within the text blob
static sk_sp<const SkTextBlob> CreateTextBlob(
    const gfxTextRun* aTextRun,
    const gfxTextRun::CompressedGlyph* aCompressedGlyph, const SkFont& aFont,
    const gfxTextRun::PropertyProvider::Spacing* aSpacing,
    uint32_t aStringStart, uint32_t aStringEnd, float aAppUnitsPerDevPixel,
    SkPoint& aTextPos, int32_t& aSpacingOffset) {
  // allocate space for the run buffer, then fill it with the glyphs
  uint32_t len =
      CountAllGlyphs(aTextRun, aCompressedGlyph, aStringStart, aStringEnd);
  if (len <= 0) {
    return nullptr;
  }

  SkTextBlobBuilder builder;
  const SkTextBlobBuilder::RunBuffer& run = builder.allocRunPos(aFont, len);

  // RTL text should be read in by glyph starting at aStringEnd - 1 down until
  // aStringStart.
  bool isRTL = aTextRun->IsRightToLeft();
  uint32_t currIndex = isRTL ? aStringEnd - 1 : aStringStart;  // textRun index
  // currIndex will be advanced by |step| until it reaches |limit|, which is the
  // final index to be handled (NOT one beyond the final index)
  int step = isRTL ? -1 : 1;
  uint32_t limit = isRTL ? aStringStart : aStringEnd - 1;

  uint32_t i = 0;  // index into the SkTextBlob we're building
  while (true) {
    // Loop exit test is below, just before we update currIndex.
    aTextPos.fX +=
        isRTL ? aSpacing[aSpacingOffset].mAfter / aAppUnitsPerDevPixel
              : aSpacing[aSpacingOffset].mBefore / aAppUnitsPerDevPixel;

    if (aCompressedGlyph[currIndex].IsSimpleGlyph()) {
      MOZ_ASSERT(i < len, "glyph count error!");
      AddSimpleGlyph(run, aCompressedGlyph[currIndex], i, aAppUnitsPerDevPixel,
                     aTextPos);
      i++;
    } else {
      // if it's detailed, potentially add multiple into run.glyphs
      uint32_t count = aCompressedGlyph[currIndex].GetGlyphCount();
      if (count > 0) {
        const auto* detailGlyph = aTextRun->GetDetailedGlyphs(currIndex, count);
        for (uint32_t d = isRTL ? count - 1 : 0; count; count--, d += step) {
          MOZ_ASSERT(i < len, "glyph count error!");
          AddDetailedGlyph(run, detailGlyph[d], i, aAppUnitsPerDevPixel,
                           aTextPos);
          i++;
        }
      }
    }
    aTextPos.fX += isRTL
                       ? aSpacing[aSpacingOffset].mBefore / aAppUnitsPerDevPixel
                       : aSpacing[aSpacingOffset].mAfter / aAppUnitsPerDevPixel;
    aSpacingOffset += step;

    if (currIndex == limit) {
      break;
    }
    currIndex += step;
  }

  MOZ_ASSERT(i == len, "glyph count error!");

  return builder.make();
}

// Given a TextBlob, the bounding lines, and the set of current intercepts this
// function adds the intercepts for the current TextBlob into the given set of
// previoulsy calculated intercepts. This set is either of length 0, or a
// multiple of 2 (since every intersection with a piece of text results in two
// intercepts: entering/exiting)
static void GetTextIntercepts(const sk_sp<const SkTextBlob>& aBlob,
                              const SkScalar aBounds[],
                              nsTArray<SkScalar>& aIntercepts) {
  uint32_t numGlyphs = 0;
  SkTextBlob::Iter::Run run;
  for (SkTextBlob::Iter iter(*aBlob); iter.next(&run);) {
    numGlyphs += run.fGlyphCount;
    if (numGlyphs >= std::numeric_limits<uint32_t>::max() / 2) {
      // Bail out if there are potentially too many intercepts; skia returns a
      // maximum of two per glyph.
      return;
    }
  }
  if (!numGlyphs) {
    return;
  }

  const auto oldLength = aIntercepts.Length();
  auto* newElements = aIntercepts.AppendElements(numGlyphs * 2, fallible);
  if (!newElements) {
    return;
  }

  // It's possible that we'll encounter a Windows exception deep inside
  // Skia's DirectWrite code while trying to get the intercepts. To avoid
  // crashing in this case, catch any such exception here and discard the
  // newly-added (and incompletely filled in) elements.
  MOZ_SEH_TRY {
    int count = aBlob->getIntercepts(aBounds, newElements);
    MOZ_DIAGNOSTIC_ASSERT(uint32_t(count) <= numGlyphs * 2,
                          "skia returned > 2 intercepts per glyph?");
    aIntercepts.TruncateLength(oldLength + count);
  }
  MOZ_SEH_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
    gfxCriticalNote << "Exception occurred getting text intercepts";
    aIntercepts.TruncateLength(oldLength);
  }
}

// This function, given a set of intercepts that represent each intersection
// between an under/overline and text, makes a series of calls to
// PaintDecorationLineInternal that paints a series of clip rects which
// implement the text-decoration-skip-ink property
// Logic for where to place each clipped rect, and the length of each rect is
// included here
static void SkipInk(nsIFrame* aFrame, DrawTarget& aDrawTarget,
                    const nsCSSRendering::PaintDecorationLineParams& aParams,
                    const nsTArray<SkScalar>& aIntercepts, Float aPadding,
                    Rect& aRect) {
  nsCSSRendering::PaintDecorationLineParams clipParams = aParams;
  const unsigned length = aIntercepts.Length();

  // For selections, this points to the selection start, which may not be at the
  // line start.
  const Float relativeTextStart =
      aParams.vertical ? aParams.pt.y : aParams.pt.x;
  const Float relativeTextEnd = relativeTextStart + aParams.lineSize.width;
  // The actual line start position needs to be adjusted by the offset of the
  // start position in the frame, because the intercept positions are based off
  // the whole text run.
  const Float absoluteLineStart = relativeTextStart - aParams.icoordInFrame;

  // Compute the min/max positions based on inset.
  const Float insetLineDrawAreaStart =
      relativeTextStart + (aParams.insetLeft - aPadding);
  const Float insetLineDrawAreaEnd =
      relativeTextEnd - (aParams.insetRight - aPadding);

  for (unsigned i = 0; i <= length; i += 2) {
    // Handle start/end edge cases and set up general case.
    // While we use the inset start/end values, it is possible the inset cuts
    // of the first intercept and into the next, so we will need to clamp
    // the dimensions in the other case too.
    SkScalar startIntercept = insetLineDrawAreaStart;
    if (i > 0) {
      startIntercept =
          std::max(aIntercepts[i - 1] + absoluteLineStart, startIntercept);
    }
    SkScalar endIntercept = insetLineDrawAreaEnd;
    if (i < length) {
      endIntercept = std::min(aIntercepts[i] + absoluteLineStart, endIntercept);
    }

    // remove padding at both ends for width
    // the start of the line is calculated so the padding removes just
    // enough so that the line starts at its normal position
    clipParams.lineSize.width =
        endIntercept - startIntercept - (2.0 * aPadding);

    // Don't draw decoration lines that have a smaller width than 1, or half
    // the line-end padding dimension.
    // This will catch the case of an intercept being fully removed by the
    // inset values, in which case the width will be negative.
    if (clipParams.lineSize.width < std::max(aPadding * 0.5, 1.0)) {
      continue;
    }

    // Start the line right after the intercept's location plus room for
    // padding; snap the rect edges to device pixels for consistent rendering
    // of dots across separate fragments of a dotted line.
    if (aParams.vertical) {
      clipParams.pt.y =
          aParams.sidewaysLeft
              ? relativeTextEnd - (endIntercept - relativeTextStart) + aPadding
              : startIntercept + aPadding;
      aRect.y = std::floor(clipParams.pt.y + 0.5);
      aRect.SetBottomEdge(
          std::floor(clipParams.pt.y + clipParams.lineSize.width + 0.5));
    } else {
      clipParams.pt.x = startIntercept + aPadding;
      aRect.x = std::floor(clipParams.pt.x + 0.5);
      aRect.SetRightEdge(
          std::floor(clipParams.pt.x + clipParams.lineSize.width + 0.5));
    }

    nsCSSRendering::PaintDecorationLineInternal(aFrame, aDrawTarget, clipParams,
                                                aRect);
  }
}

void nsCSSRendering::PaintDecorationLine(
    nsIFrame* aFrame, DrawTarget& aDrawTarget,
    const PaintDecorationLineParams& aParams) {
  NS_ASSERTION(aParams.style != StyleTextDecorationStyle::None,
               "aStyle is none");

  mozilla::layout::TextDrawTarget* textDrawer = nullptr;
  if (aDrawTarget.GetBackendType() == BackendType::WEBRENDER_TEXT) {
    textDrawer = static_cast<mozilla::layout::TextDrawTarget*>(&aDrawTarget);
  }

  // Under layout.disable-pixel-alignment, the WebRender text path device-snaps
  // the decoration at frame time, so leave the geometry unsnapped here. Without
  // that path (software / non-WebRender-text drawing such as drawSnapshot)
  // there is no later snap, so pre-snap the geometry as usual (bug 2004666).
  const bool snapToPixels =
      !StaticPrefs::layout_disable_pixel_alignment() || !textDrawer;

  Rect rect =
      ToRect(GetTextDecorationRectInternal(aParams.pt, aParams, snapToPixels));
  if (rect.IsEmpty() || !rect.Intersects(aParams.dirtyRect)) {
    return;
  }

  if (aParams.decoration != StyleTextDecorationLine::UNDERLINE &&
      aParams.decoration != StyleTextDecorationLine::OVERLINE &&
      aParams.decoration != StyleTextDecorationLine::LINE_THROUGH) {
    MOZ_ASSERT_UNREACHABLE("Invalid text decoration value");
    return;
  }

  // Check if decoration line will skip past ascenders/descenders
  // text-decoration-skip-ink only applies to overlines/underlines
  bool skipInkEnabled =
      aParams.skipInk != mozilla::StyleTextDecorationSkipInk::None &&
      aParams.decoration != StyleTextDecorationLine::LINE_THROUGH &&
      aParams.allowInkSkipping && aFrame->IsTextFrame();

  if (!skipInkEnabled || aParams.glyphRange.Length() == 0) {
    PaintDecorationLineInternal(aFrame, aDrawTarget, aParams, rect);
    return;
  }

  // Must be a text frame, otherwise skipInkEnabled (above) would be false.
  nsTextFrame* textFrame = static_cast<nsTextFrame*>(aFrame);

  // get text run and current text offset (for line wrapping)
  gfxTextRun* textRun =
      textFrame->GetTextRun(nsTextFrame::TextRunType::eInflated);

  // used for conversions from app units to device pixels
  int32_t appUnitsPerDevPixel = aFrame->PresContext()->AppUnitsPerDevPixel();

  // pointer to the array of glyphs for this TextRun
  gfxTextRun::CompressedGlyph* characterGlyphs = textRun->GetCharacterGlyphs();

  // get positioning info
  SkPoint textPos = {0, aParams.baselineOffset};
  SkScalar bounds[] = {0, 0};
  Float oneCSSPixel = aFrame->PresContext()->CSSPixelsToDevPixels(1.0f);
  if (!textRun->UseCenterBaseline()) {
    GetPositioning(aParams, rect, oneCSSPixel, 0, bounds);
  }

  // array for the text intercepts
  AutoTArray<SkScalar, 256> intercepts;

  // array for spacing data
  AutoTArray<gfxTextRun::PropertyProvider::Spacing, 64> spacing;
  spacing.SetLength(aParams.glyphRange.Length());
  if (aParams.provider != nullptr) {
    aParams.provider->GetSpacing(aParams.glyphRange, spacing.Elements());
  }

  // loop through each glyph run
  // in most cases there will only be one
  bool isRTL = textRun->IsRightToLeft();
  int32_t spacingOffset = isRTL ? aParams.glyphRange.Length() - 1 : 0;
  gfxTextRun::GlyphRunIterator iter(textRun, aParams.glyphRange, isRTL);

  // For any glyph run where we don't actually do skipping, we'll need to
  // advance the current position by its width.
  // (For runs we do process, CreateTextBlob will update the position.)
  auto currentGlyphRunAdvance = [&]() {
    return textRun->GetAdvanceWidth(
               gfxTextRun::Range(iter.StringStart(), iter.StringEnd()),
               aParams.provider) /
           appUnitsPerDevPixel;
  };

  for (; !iter.AtEnd(); iter.NextRun()) {
    if (iter.GlyphRun()->mOrientation ==
            mozilla::gfx::ShapedTextFlags::TEXT_ORIENT_VERTICAL_UPRIGHT ||
        (iter.GlyphRun()->mIsCJK &&
         aParams.skipInk == mozilla::StyleTextDecorationSkipInk::Auto)) {
      // We don't support upright text in vertical modes currently
      // (see https://bugzilla.mozilla.org/show_bug.cgi?id=1572294),
      // but we do need to update textPos so that following runs will be
      // correctly positioned.
      // We also don't apply skip-ink to CJK text runs because many fonts
      // have an underline that looks really bad if this is done
      // (see https://bugzilla.mozilla.org/show_bug.cgi?id=1573249),
      // when skip-ink is set to 'auto'.
      textPos.fX += currentGlyphRunAdvance();
      continue;
    }

    gfxFont* font = iter.GlyphRun()->mFont;
    // Don't try to apply skip-ink to 'sbix' fonts like Apple Color Emoji,
    // because old macOS (10.9) may crash trying to retrieve glyph paths
    // that don't exist.
    if (font->GetFontEntry()->HasFontTable(TRUETYPE_TAG('s', 'b', 'i', 'x'))) {
      textPos.fX += currentGlyphRunAdvance();
      continue;
    }

    // get a Skia version of the glyph run's font
    SkFont skiafont;
    if (!GetSkFontFromGfxFont(aDrawTarget, font, skiafont)) {
      PaintDecorationLineInternal(aFrame, aDrawTarget, aParams, rect);
      return;
    }

    // Create a text blob with correctly positioned glyphs. This also updates
    // textPos.fX with the advance of the glyphs.
    sk_sp<const SkTextBlob> textBlob =
        CreateTextBlob(textRun, characterGlyphs, skiafont, spacing.Elements(),
                       iter.StringStart(), iter.StringEnd(),
                       (float)appUnitsPerDevPixel, textPos, spacingOffset);

    if (!textBlob) {
      textPos.fX += currentGlyphRunAdvance();
      continue;
    }

    if (textRun->UseCenterBaseline()) {
      // writing modes that use a center baseline need to be adjusted on a
      // font-by-font basis since Skia lines up the text on a alphabetic
      // baseline, but for some vertical-* writing modes the offset is from the
      // center.
      gfxFont::Metrics metrics = font->GetMetrics(nsFontMetrics::eHorizontal);
      Float centerToBaseline = (metrics.emAscent - metrics.emDescent) / 2.0f;
      GetPositioning(aParams, rect, oneCSSPixel, centerToBaseline, bounds);
    }

    // compute the text intercepts that need to be skipped
    GetTextIntercepts(textBlob, bounds, intercepts);
  }
  bool needsSkipInk = intercepts.Length() > 0;

  if (needsSkipInk) {
    // Padding between glyph intercepts and the decoration line: we use the
    // decoration line thickness, clamped to a minimum of 1px and a maximum
    // of 0.2em.
    Float padding =
        std::min(std::max(aParams.lineSize.height, oneCSSPixel),
                 Float(textRun->GetFontGroup()->GetStyle()->size / 5.0));
    SkipInk(aFrame, aDrawTarget, aParams, intercepts, padding, rect);
  } else {
    PaintDecorationLineInternal(aFrame, aDrawTarget, aParams, rect);
  }
}

void nsCSSRendering::PaintDecorationLineInternal(
    nsIFrame* aFrame, DrawTarget& aDrawTarget,
    const PaintDecorationLineParams& aParams, Rect aRect) {
  const Float lineThickness = aParams.lineSize.height;
  DeviceColor color = ToDeviceColor(aParams.color);
  ColorPattern colorPat(color);
  StrokeOptions strokeOptions(aParams.lineSize.height);
  DrawOptions drawOptions;

  Float dash[2];

  AutoPopClips autoPopClips(&aDrawTarget);

  mozilla::layout::TextDrawTarget* textDrawer = nullptr;
  if (aDrawTarget.GetBackendType() == BackendType::WEBRENDER_TEXT) {
    textDrawer = static_cast<mozilla::layout::TextDrawTarget*>(&aDrawTarget);
  }

  switch (aParams.style) {
    case StyleTextDecorationStyle::Solid:
    case StyleTextDecorationStyle::Double:
      break;
    case StyleTextDecorationStyle::Dashed: {
      autoPopClips.PushClipRect(aRect);
      Float dashWidth = lineThickness * DOT_LENGTH * DASH_LENGTH;
      dash[0] = dashWidth;
      dash[1] = dashWidth;
      strokeOptions.mDashPattern = dash;
      strokeOptions.mDashLength = std::size(dash);
      strokeOptions.mLineCap = CapStyle::BUTT;
      aRect = ExpandPaintingRectForDecorationLine(
          aFrame, aParams.style, aRect, aParams.icoordInFrame, dashWidth * 2,
          aParams.vertical);
      // We should continue to draw the last dash even if it is not in the rect.
      aRect.width += dashWidth;
      break;
    }
    case StyleTextDecorationStyle::Dotted: {
      autoPopClips.PushClipRect(aRect);
      Float dashWidth = lineThickness * DOT_LENGTH;
      if (lineThickness > 2.0) {
        dash[0] = 0.f;
        dash[1] = dashWidth * 2.f;
        strokeOptions.mLineCap = CapStyle::ROUND;
      } else {
        dash[0] = dashWidth;
        dash[1] = dashWidth;
      }
      strokeOptions.mDashPattern = dash;
      strokeOptions.mDashLength = std::size(dash);
      aRect = ExpandPaintingRectForDecorationLine(
          aFrame, aParams.style, aRect, aParams.icoordInFrame, dashWidth * 2,
          aParams.vertical);
      // We should continue to draw the last dot even if it is not in the rect.
      aRect.width += dashWidth;
      break;
    }
    case StyleTextDecorationStyle::Wavy:
      autoPopClips.PushClipRect(aRect);
      if (lineThickness > 2.0) {
        drawOptions.mAntialiasMode = AntialiasMode::SUBPIXEL;
      } else {
        // Don't use anti-aliasing here.  Because looks like lighter color wavy
        // line at this case.  And probably, users don't think the
        // non-anti-aliased wavy line is not pretty.
        drawOptions.mAntialiasMode = AntialiasMode::NONE;
      }
      break;
    default:
      NS_ERROR("Invalid style value!");
      return;
  }

  // The block-direction position should be set to the middle of the line.
  if (aParams.vertical) {
    aRect.x += lineThickness / 2;
  } else {
    aRect.y += lineThickness / 2;
  }

  switch (aParams.style) {
    case StyleTextDecorationStyle::Solid:
    case StyleTextDecorationStyle::Dotted:
    case StyleTextDecorationStyle::Dashed: {
      Point p1 = aRect.TopLeft();
      Point p2 = aParams.vertical ? aRect.BottomLeft() : aRect.TopRight();
      if (textDrawer) {
        textDrawer->AppendDecoration(p1, p2, lineThickness, aParams.vertical,
                                     color, aParams.style);
      } else {
        aDrawTarget.StrokeLine(p1, p2, colorPat, strokeOptions, drawOptions);
      }
      return;
    }
    case StyleTextDecorationStyle::Double: {
      /**
       *  We are drawing double line as:
       *
       * +-------------------------------------------+
       * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| ^
       * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| | lineThickness
       * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| v
       * |                                           |
       * |                                           |
       * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| ^
       * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| | lineThickness
       * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| v
       * +-------------------------------------------+
       */
      Point p1a = aRect.TopLeft();
      Point p2a = aParams.vertical ? aRect.BottomLeft() : aRect.TopRight();

      if (aParams.vertical) {
        aRect.width -= lineThickness;
      } else {
        aRect.height -= lineThickness;
      }

      Point p1b = aParams.vertical ? aRect.TopRight() : aRect.BottomLeft();
      Point p2b = aRect.BottomRight();

      if (textDrawer) {
        textDrawer->AppendDecoration(p1a, p2a, lineThickness, aParams.vertical,
                                     color, StyleTextDecorationStyle::Solid);
        textDrawer->AppendDecoration(p1b, p2b, lineThickness, aParams.vertical,
                                     color, StyleTextDecorationStyle::Solid);
      } else {
        aDrawTarget.StrokeLine(p1a, p2a, colorPat, strokeOptions, drawOptions);
        aDrawTarget.StrokeLine(p1b, p2b, colorPat, strokeOptions, drawOptions);
      }
      return;
    }
    case StyleTextDecorationStyle::Wavy: {
      /**
       *  We are drawing wavy line as:
       *
       *  P: Path, X: Painted pixel
       *
       *     +---------------------------------------+
       *   XX|X            XXXXXX            XXXXXX  |
       *   PP|PX          XPPPPPPX          XPPPPPPX |    ^
       *   XX|XPX        XPXXXXXXPX        XPXXXXXXPX|    |
       *     | XPX      XPX      XPX      XPX      XP|X   |adv
       *     |  XPXXXXXXPX        XPXXXXXXPX        X|PX  |
       *     |   XPPPPPPX          XPPPPPPX          |XPX v
       *     |    XXXXXX            XXXXXX           | XX
       *     +---------------------------------------+
       *      <---><--->                                ^
       *      adv  flatLengthAtVertex                   rightMost
       *
       *  1. Always starts from top-left of the drawing area, however, we need
       *     to draw  the line from outside of the rect.  Because the start
       *     point of the line is not good style if we draw from inside it.
       *  2. First, draw horizontal line from outside the rect to top-left of
       *     the rect;
       *  3. Goes down to bottom of the area at 45 degrees.
       *  4. Slides to right horizontaly, see |flatLengthAtVertex|.
       *  5. Goes up to top of the area at 45 degrees.
       *  6. Slides to right horizontaly.
       *  7. Repeat from 2 until reached to right-most edge of the area.
       *
       * In the vertical case, swap horizontal and vertical coordinates and
       * directions in the above description.
       */

      Float& rectICoord = aParams.vertical ? aRect.y : aRect.x;
      Float& rectISize = aParams.vertical ? aRect.height : aRect.width;
      const Float rectBSize = aParams.vertical ? aRect.width : aRect.height;

      const Float adv = rectBSize - lineThickness;
      const Float flatLengthAtVertex =
          std::max((lineThickness - 1.0) * 2.0, 1.0);

      // Align the start of wavy lines to the nearest ancestor block.
      const Float cycleLength = 2 * (adv + flatLengthAtVertex);
      aRect = ExpandPaintingRectForDecorationLine(
          aFrame, aParams.style, aRect, aParams.icoordInFrame, cycleLength,
          aParams.vertical);

      if (textDrawer) {
        // Undo attempted centering
        Float& rectBCoord = aParams.vertical ? aRect.x : aRect.y;
        rectBCoord -= lineThickness / 2;

        textDrawer->AppendWavyDecoration(aRect, lineThickness, aParams.vertical,
                                         color);
        return;
      }

      // figure out if we can trim whole cycles from the left and right edges
      // of the line, to try and avoid creating an unnecessarily long and
      // complex path (but don't do this for webrender, )
      const Float dirtyRectICoord =
          aParams.vertical ? aParams.dirtyRect.y : aParams.dirtyRect.x;
      int32_t skipCycles = floor((dirtyRectICoord - rectICoord) / cycleLength);
      if (skipCycles > 0) {
        rectICoord += skipCycles * cycleLength;
        rectISize -= skipCycles * cycleLength;
      }

      rectICoord += lineThickness / 2.0;

      Point pt(aRect.TopLeft());
      Float& ptICoord = aParams.vertical ? pt.y.value : pt.x.value;
      Float& ptBCoord = aParams.vertical ? pt.x.value : pt.y.value;
      if (aParams.vertical) {
        ptBCoord += adv;
      }
      Float iCoordLimit = ptICoord + rectISize + lineThickness;

      const Float dirtyRectIMost = aParams.vertical ? aParams.dirtyRect.YMost()
                                                    : aParams.dirtyRect.XMost();
      skipCycles = floor((iCoordLimit - dirtyRectIMost) / cycleLength);
      if (skipCycles > 0) {
        iCoordLimit -= skipCycles * cycleLength;
      }

      RefPtr<PathBuilder> builder = aDrawTarget.CreatePathBuilder();
      RefPtr<Path> path;

      ptICoord -= lineThickness;
      builder->MoveTo(pt);  // 1

      ptICoord = rectICoord;
      builder->LineTo(pt);  // 2

      // In vertical mode, to go "down" relative to the text we need to
      // decrease the block coordinate, whereas in horizontal we increase
      // it. So the sense of this flag is effectively inverted.
      bool goDown = !aParams.vertical;
      uint32_t iter = 0;
      while (ptICoord < iCoordLimit) {
        if (++iter > 1000) {
          // stroke the current path and start again, to avoid pathological
          // behavior in cairo with huge numbers of path segments
          path = builder->Finish();
          aDrawTarget.Stroke(path, colorPat, strokeOptions, drawOptions);
          builder = aDrawTarget.CreatePathBuilder();
          builder->MoveTo(pt);
          iter = 0;
        }
        ptICoord += adv;
        ptBCoord += goDown ? adv : -adv;

        builder->LineTo(pt);  // 3 and 5

        ptICoord += flatLengthAtVertex;
        builder->LineTo(pt);  // 4 and 6

        goDown = !goDown;
      }
      path = builder->Finish();
      aDrawTarget.Stroke(path, colorPat, strokeOptions, drawOptions);
      return;
    }
    default:
      NS_ERROR("Invalid style value!");
  }
}

Rect nsCSSRendering::DecorationLineToPath(
    const PaintDecorationLineParams& aParams) {
  NS_ASSERTION(aParams.style != StyleTextDecorationStyle::None,
               "aStyle is none");

  Rect path;  // To benefit from RVO, we return this from all return points

  Rect rect =
      ToRect(GetTextDecorationRectInternal(aParams.pt, aParams,
                                           /* aSnapToDevicePixels */ true));
  if (rect.IsEmpty() || !rect.Intersects(aParams.dirtyRect)) {
    return path;
  }

  if (aParams.decoration != StyleTextDecorationLine::UNDERLINE &&
      aParams.decoration != StyleTextDecorationLine::OVERLINE &&
      aParams.decoration != StyleTextDecorationLine::LINE_THROUGH) {
    MOZ_ASSERT_UNREACHABLE("Invalid text decoration value");
    return path;
  }

  if (aParams.style != StyleTextDecorationStyle::Solid) {
    // For the moment, we support only solid text decorations.
    return path;
  }

  const Float lineThickness = aParams.lineSize.height;
  // The block-direction position should be set to the middle of the line.
  if (aParams.vertical) {
    rect.x += lineThickness / 2;
    path = Rect(rect.TopLeft() - Point(lineThickness / 2, 0.0),
                Size(lineThickness, rect.Height()));
  } else {
    rect.y += lineThickness / 2;
    path = Rect(rect.TopLeft() - Point(0.0, lineThickness / 2),
                Size(rect.Width(), lineThickness));
  }

  return path;
}

nsRect nsCSSRendering::GetTextDecorationRect(
    nsPresContext* aPresContext, const DecorationRectParams& aParams) {
  NS_ASSERTION(aPresContext, "aPresContext is null");
  NS_ASSERTION(aParams.style != StyleTextDecorationStyle::None,
               "aStyle is none");

  gfxRect rect = GetTextDecorationRectInternal(Point(0, 0), aParams,
                                               /* aSnapToDevicePixels */ true);
  // The rect values are already rounded to nearest device pixels.
  nsRect r;
  r.x = aPresContext->GfxUnitsToAppUnits(rect.X());
  r.y = aPresContext->GfxUnitsToAppUnits(rect.Y());
  r.width = aPresContext->GfxUnitsToAppUnits(rect.Width());
  r.height = aPresContext->GfxUnitsToAppUnits(rect.Height());
  return r;
}

gfxRect nsCSSRendering::GetTextDecorationRectInternal(
    const Point& aPt, const DecorationRectParams& aParams,
    bool aSnapToDevicePixels) {
  NS_ASSERTION(aParams.style <= StyleTextDecorationStyle::Wavy,
               "Invalid aStyle value");

  if (aParams.style == StyleTextDecorationStyle::None) {
    return gfxRect(0, 0, 0, 0);
  }

  bool canLiftUnderline = aParams.descentLimit >= 0.0;

  gfxFloat iCoord = aParams.vertical ? aPt.y : aPt.x;
  gfxFloat bCoord = aParams.vertical ? aPt.x : aPt.y;

  // 'left' and 'right' are relative to the line, so for vertical writing modes
  // they will actually become top and bottom of the rendered line.
  // Similarly, aLineSize.width and .height are actually length and thickness
  // of the line, which runs horizontally or vertically according to aVertical.
  // With pixel alignment disabled, don't pre-snap the decoration's inline
  // position when it will be device-snapped downstream by WebRender's
  // frame-time pass (`aSnapToDevicePixels == false`): leaving it fractional
  // lets that pass snap it against the live (post-sticky/scroll-normalization)
  // transform. Pre-snapping here bakes in an integer local position that maps
  // to a different world position as the spatial tree flips between paints, so
  // the frame-time snap rounds it inconsistently and the line jitters
  // (bug 2004666). When there is no such pass (software / non-WebRender-text
  // drawing, e.g. drawSnapshot), the caller asks us to pre-snap as before.
  const bool snapToPixels = aSnapToDevicePixels;
  const gfxFloat left = snapToPixels ? floor(iCoord + 0.5) : iCoord,
                 right = snapToPixels
                             ? floor(iCoord + aParams.lineSize.width + 0.5)
                             : iCoord + aParams.lineSize.width;

  // We compute |r| as if for a horizontal text run, and then swap vertical
  // and horizontal coordinates at the end if vertical was requested.
  gfxRect r(left, 0, right - left, 0);

  const gfxFloat lineThickness = aParams.lineSize.height;
  gfxFloat defaultLineThickness = NS_round(aParams.defaultLineThickness);
  defaultLineThickness = std::max(defaultLineThickness, 1.0);

  gfxFloat ascent = NS_round(aParams.ascent);
  gfxFloat descentLimit = floor(aParams.descentLimit);

  gfxFloat suggestedMaxRectHeight =
      std::max(std::min(ascent, descentLimit), 1.0);
  r.height = lineThickness;
  if (aParams.style == StyleTextDecorationStyle::Double) {
    /**
     *  We will draw double line as:
     *
     * +-------------------------------------------+
     * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| ^
     * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| | lineThickness
     * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| v
     * |                                           | ^
     * |                                           | | gap
     * |                                           | v
     * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| ^
     * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| | lineThickness
     * |XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX| v
     * +-------------------------------------------+
     */
    gfxFloat gap = NS_round(lineThickness / 2.0);
    gap = std::max(gap, 1.0);
    r.height = lineThickness * 2.0 + gap;
    if (canLiftUnderline) {
      if (r.Height() > suggestedMaxRectHeight) {
        // Don't shrink the line height, because the thickness has some meaning.
        // We can just shrink the gap at this time.
        r.height = std::max(suggestedMaxRectHeight, lineThickness * 2.0 + 1.0);
      }
    }
  } else if (aParams.style == StyleTextDecorationStyle::Wavy) {
    /**
     *  We will draw wavy line as:
     *
     * +-------------------------------------------+
     * |XXXXX            XXXXXX            XXXXXX  | ^
     * |XXXXXX          XXXXXXXX          XXXXXXXX | | lineThickness
     * |XXXXXXX        XXXXXXXXXX        XXXXXXXXXX| v
     * |     XXX      XXX      XXX      XXX      XX|
     * |      XXXXXXXXXX        XXXXXXXXXX        X|
     * |       XXXXXXXX          XXXXXXXX          |
     * |        XXXXXX            XXXXXX           |
     * +-------------------------------------------+
     */
    r.height = lineThickness > 2.0 ? lineThickness * 4.0 : lineThickness * 3.0;
    if (canLiftUnderline) {
      if (r.Height() > suggestedMaxRectHeight) {
        // Don't shrink the line height even if there is not enough space,
        // because the thickness has some meaning.  E.g., the 1px wavy line and
        // 2px wavy line can be used for different meaning in IME selections
        // at same time.
        r.height = std::max(suggestedMaxRectHeight, lineThickness * 2.0);
      }
    }
  }

  gfxFloat baseline = snapToPixels ? floor(bCoord + aParams.ascent + 0.5)
                                   : bCoord + aParams.ascent;

  // Calculate adjusted offset based on writing-mode/orientation and thickness
  // of decoration line. The input value aParams.offset is the nominal position
  // (offset from baseline) where we would draw a single, infinitely-thin line;
  // but for a wavy or double line, we'll need to move the bounding rect of the
  // decoration outwards from the baseline so that an underline remains below
  // the glyphs, and an overline above them, despite the increased block-dir
  // extent of the decoration.
  //
  // So adjustments by r.Height() are used to make the wider line styles (wavy
  // and double) "grow" in the appropriate direction compared to the basic
  // single line.
  //
  // Note that at this point, the decoration rect is being calculated in line-
  // relative coordinates, where 'x' is line-rightwards, and 'y' is line-
  // upwards. We'll swap them to be physical coords at the end.
  gfxFloat offset = 0.0;

  if (aParams.decoration == StyleTextDecorationLine::UNDERLINE) {
    offset = aParams.offset;
    if (canLiftUnderline) {
      if (descentLimit < -offset + r.Height()) {
        // If we can ignore the offset and the decoration line is overflowing,
        // we should align the bottom edge of the decoration line rect if it's
        // possible.  Otherwise, we should lift up the top edge of the rect as
        // far as possible.
        gfxFloat offsetBottomAligned = -descentLimit + r.Height();
        gfxFloat offsetTopAligned = 0.0;
        offset = std::min(offsetBottomAligned, offsetTopAligned);
      }
    }
  } else if (aParams.decoration == StyleTextDecorationLine::OVERLINE) {
    // For overline, we adjust the offset by defaultlineThickness (the default
    // thickness of a single decoration line) because empirically it looks
    // better to draw the overline just inside rather than outside the font's
    // ascent, which is what nsTextFrame passes as aParams.offset (as fonts
    // don't provide an explicit overline-offset).
    offset = aParams.offset - defaultLineThickness + r.Height();
  } else if (aParams.decoration == StyleTextDecorationLine::LINE_THROUGH) {
    // To maintain a consistent mid-point for line-through decorations,
    // we adjust the offset by half of the decoration rect's height.
    gfxFloat extra = floor(r.Height() / 2.0 + 0.5);
    extra = std::max(extra, lineThickness);
    // computes offset for when user specifies a decoration width since
    // aParams.offset is derived from the font metric's line height
    gfxFloat decorationThicknessOffset =
        (lineThickness - defaultLineThickness) / 2.0;
    offset = aParams.offset - lineThickness + extra + decorationThicknessOffset;
  } else {
    MOZ_ASSERT_UNREACHABLE("Invalid text decoration value");
  }

  // Take text decoration inset into account.
  r.x += aParams.insetLeft;
  r.width -= aParams.insetLeft + aParams.insetRight;
  r.width = std::max(r.width, 0.0);

  // Convert line-relative coordinate system (x = line-right, y = line-up)
  // to physical coords, and move the decoration rect to the calculated
  // offset from baseline.
  if (aParams.vertical) {
    std::swap(r.x, r.y);
    std::swap(r.width, r.height);
    // line-upwards in vertical mode = physical-right, so we /add/ offset
    // to baseline. Except in sideways-lr mode, where line-upwards will be
    // physical leftwards.
    if (aParams.sidewaysLeft) {
      r.x = baseline - floor(offset + 0.5);
    } else {
      r.x = baseline + floor(offset - r.Width() + 0.5);
    }
  } else {
    // line-upwards in horizontal mode = physical-up, but our physical coord
    // system works downwards, so we /subtract/ offset from baseline.
    r.y = baseline - floor(offset + 0.5);
  }

  return r;
}

#define MAX_BLUR_RADIUS 300
#define MAX_SPREAD_RADIUS 50

static inline gfxPoint ComputeBlurStdDev(nscoord aBlurRadius,
                                         int32_t aAppUnitsPerDevPixel,
                                         gfxFloat aScaleX, gfxFloat aScaleY) {
  // http://dev.w3.org/csswg/css3-background/#box-shadow says that the
  // standard deviation of the blur should be half the given blur value.
  gfxFloat blurStdDev = gfxFloat(aBlurRadius) / gfxFloat(aAppUnitsPerDevPixel);

  return gfxPoint(
      std::min((blurStdDev * aScaleX), gfxFloat(MAX_BLUR_RADIUS)) / 2.0,
      std::min((blurStdDev * aScaleY), gfxFloat(MAX_BLUR_RADIUS)) / 2.0);
}

static inline IntSize ComputeBlurRadius(nscoord aBlurRadius,
                                        int32_t aAppUnitsPerDevPixel,
                                        gfxFloat aScaleX = 1.0,
                                        gfxFloat aScaleY = 1.0) {
  gfxPoint scaledBlurStdDev =
      ComputeBlurStdDev(aBlurRadius, aAppUnitsPerDevPixel, aScaleX, aScaleY);
  return gfxGaussianBlur::CalculateBlurRadius(scaledBlurStdDev);
}

// -----
// nsContextBoxBlur
// -----
gfxContext* nsContextBoxBlur::Init(const nsRect& aRect, nscoord aSpreadRadius,
                                   nscoord aBlurRadius,
                                   int32_t aAppUnitsPerDevPixel,
                                   gfxContext* aDestinationCtx,
                                   const nsRect& aDirtyRect,
                                   const gfxRect* aSkipRect, uint32_t aFlags) {
  if (aRect.IsEmpty()) {
    mContext = nullptr;
    return nullptr;
  }

  IntSize blurRadius;
  IntSize spreadRadius;
  GetBlurAndSpreadRadius(aDestinationCtx->GetDrawTarget(), aAppUnitsPerDevPixel,
                         aBlurRadius, aSpreadRadius, blurRadius, spreadRadius);

  mDestinationCtx = aDestinationCtx;

  // If not blurring, draw directly onto the destination device
  if (blurRadius.width <= 0 && blurRadius.height <= 0 &&
      spreadRadius.width <= 0 && spreadRadius.height <= 0 &&
      !(aFlags & FORCE_MASK)) {
    mContext = aDestinationCtx;
    return mContext;
  }

  // Convert from app units to device pixels
  gfxRect rect = nsLayoutUtils::RectToGfxRect(aRect, aAppUnitsPerDevPixel);

  gfxRect dirtyRect =
      nsLayoutUtils::RectToGfxRect(aDirtyRect, aAppUnitsPerDevPixel);
  dirtyRect.RoundOut();

  gfxMatrix transform = aDestinationCtx->CurrentMatrixDouble();
  rect = transform.TransformBounds(rect);

  mPreTransformed = !transform.IsIdentity();

  // Create the temporary surface for blurring
  dirtyRect = transform.TransformBounds(dirtyRect);
  if (aSkipRect) {
    gfxRect skipRect = transform.TransformBounds(*aSkipRect);
    mOwnedContext = mGaussianBlur.Init(aDestinationCtx, rect, spreadRadius,
                                       blurRadius, &dirtyRect, &skipRect);
  } else {
    mOwnedContext = mGaussianBlur.Init(aDestinationCtx, rect, spreadRadius,
                                       blurRadius, &dirtyRect, nullptr);
  }
  mContext = mOwnedContext.get();

  if (mContext) {
    // we don't need to blur if skipRect is equal to rect
    // and mContext will be nullptr
    mContext->Multiply(transform);
  }
  return mContext;
}

void nsContextBoxBlur::DoPaint() {
  if (mContext == mDestinationCtx) {
    return;
  }

  gfxContextMatrixAutoSaveRestore saveMatrix(mDestinationCtx);

  if (mPreTransformed) {
    mDestinationCtx->SetMatrix(Matrix());
  }

  mGaussianBlur.Paint(mDestinationCtx);
}

gfxContext* nsContextBoxBlur::GetContext() { return mContext; }

/* static */
nsMargin nsContextBoxBlur::GetBlurRadiusMargin(nscoord aBlurRadius,
                                               int32_t aAppUnitsPerDevPixel) {
  IntSize blurRadius = ComputeBlurRadius(aBlurRadius, aAppUnitsPerDevPixel);

  nsMargin result;
  result.top = result.bottom = blurRadius.height * aAppUnitsPerDevPixel;
  result.left = result.right = blurRadius.width * aAppUnitsPerDevPixel;
  return result;
}

/* static */
void nsContextBoxBlur::BlurRectangle(
    gfxContext* aDestinationCtx, const nsRect& aRect,
    int32_t aAppUnitsPerDevPixel, RectCornerRadii* aCornerRadii,
    nscoord aBlurRadius, const sRGBColor& aShadowColor,
    const nsRect& aDirtyRect, const gfxRect& aSkipRect) {
  DrawTarget& aDestDrawTarget = *aDestinationCtx->GetDrawTarget();

  if (aRect.IsEmpty()) {
    return;
  }

  Rect shadowGfxRect = NSRectToRect(aRect, aAppUnitsPerDevPixel);

  if (aBlurRadius <= 0) {
    ColorPattern color(ToDeviceColor(aShadowColor));
    if (aCornerRadii) {
      RefPtr<Path> roundedRect =
          MakePathForRoundedRect(aDestDrawTarget, shadowGfxRect, *aCornerRadii);
      aDestDrawTarget.Fill(roundedRect, color);
    } else {
      aDestDrawTarget.FillRect(shadowGfxRect, color);
    }
    return;
  }

  gfxFloat scaleX = 1;
  gfxFloat scaleY = 1;

  // Do blurs in device space when possible.
  // Chrome/Skia always does the blurs in device space
  // and will sometimes get incorrect results (e.g. rotated blurs)
  gfxMatrix transform = aDestinationCtx->CurrentMatrixDouble();
  // XXX: we could probably handle negative scales but for now it's easier just
  // to fallback
  if (!transform.HasNonAxisAlignedTransform() && transform._11 > 0.0 &&
      transform._22 > 0.0) {
    scaleX = transform._11;
    scaleY = transform._22;
    aDestinationCtx->SetMatrix(Matrix());
  } else {
    transform = gfxMatrix();
  }

  gfxPoint blurStdDev =
      ComputeBlurStdDev(aBlurRadius, aAppUnitsPerDevPixel, scaleX, scaleY);

  gfxRect dirtyRect =
      nsLayoutUtils::RectToGfxRect(aDirtyRect, aAppUnitsPerDevPixel);
  dirtyRect.RoundOut();

  gfxRect shadowThebesRect =
      transform.TransformBounds(ThebesRect(shadowGfxRect));
  dirtyRect = transform.TransformBounds(dirtyRect);
  gfxRect skipRect = transform.TransformBounds(aSkipRect);

  if (aCornerRadii) {
    aCornerRadii->Scale(scaleX, scaleY);
  }

  gfxGaussianBlur::BlurRectangle(aDestinationCtx, shadowThebesRect,
                                 aCornerRadii, blurStdDev, aShadowColor,
                                 dirtyRect, skipRect);
}

/* static */
void nsContextBoxBlur::GetBlurAndSpreadRadius(
    DrawTarget* aDestDrawTarget, int32_t aAppUnitsPerDevPixel,
    nscoord aBlurRadius, nscoord aSpreadRadius, IntSize& aOutBlurRadius,
    IntSize& aOutSpreadRadius, bool aConstrainSpreadRadius) {
  // Do blurs in device space when possible.
  // Chrome/Skia always does the blurs in device space
  // and will sometimes get incorrect results (e.g. rotated blurs)
  Matrix transform = aDestDrawTarget->GetTransform();
  // XXX: we could probably handle negative scales but for now it's easier just
  // to fallback
  gfxFloat scaleX, scaleY;
  if (transform.HasNonAxisAlignedTransform() || transform._11 <= 0.0 ||
      transform._22 <= 0.0) {
    scaleX = 1;
    scaleY = 1;
  } else {
    scaleX = transform._11;
    scaleY = transform._22;
  }

  // compute a large or smaller blur radius
  aOutBlurRadius =
      ComputeBlurRadius(aBlurRadius, aAppUnitsPerDevPixel, scaleX, scaleY);
  aOutSpreadRadius =
      IntSize(int32_t(aSpreadRadius * scaleX / aAppUnitsPerDevPixel),
              int32_t(aSpreadRadius * scaleY / aAppUnitsPerDevPixel));

  if (aConstrainSpreadRadius) {
    aOutSpreadRadius.width =
        std::min(aOutSpreadRadius.width, int32_t(MAX_SPREAD_RADIUS));
    aOutSpreadRadius.height =
        std::min(aOutSpreadRadius.height, int32_t(MAX_SPREAD_RADIUS));
  }
}

/* static */
bool nsContextBoxBlur::InsetBoxBlur(
    gfxContext* aDestinationCtx, Rect aDestinationRect, Rect aShadowClipRect,
    sRGBColor& aShadowColor, nscoord aBlurRadiusAppUnits,
    nscoord aSpreadDistanceAppUnits, int32_t aAppUnitsPerDevPixel,
    bool aHasBorderRadius, RectCornerRadii& aInnerClipRectRadii, Rect aSkipRect,
    Point aShadowOffset) {
  if (aDestinationRect.IsEmpty()) {
    mContext = nullptr;
    return false;
  }

  gfxContextAutoSaveRestore autoRestore(aDestinationCtx);

  IntSize blurRadius;
  IntSize spreadRadius;
  // Convert the blur and spread radius to device pixels
  bool constrainSpreadRadius = false;
  GetBlurAndSpreadRadius(aDestinationCtx->GetDrawTarget(), aAppUnitsPerDevPixel,
                         aBlurRadiusAppUnits, aSpreadDistanceAppUnits,
                         blurRadius, spreadRadius, constrainSpreadRadius);

  // The blur and spread radius are scaled already, so scale all
  // input data to the blur. This way, we don't have to scale the min
  // inset blur to the invert of the dest context, then rescale it back
  // when we draw to the destination surface.
  auto scale = aDestinationCtx->CurrentMatrix().ScaleFactors();
  Matrix transform = aDestinationCtx->CurrentMatrix();

  // XXX: we could probably handle negative scales but for now it's easier just
  // to fallback
  if (!transform.HasNonAxisAlignedTransform() && transform._11 > 0.0 &&
      transform._22 > 0.0) {
    // If we don't have a rotation, we're pre-transforming all the rects.
    aDestinationCtx->SetMatrix(Matrix());
  } else {
    // Don't touch anything, we have a rotation.
    transform = Matrix();
  }

  Rect transformedDestRect = transform.TransformBounds(aDestinationRect);
  Rect transformedShadowClipRect = transform.TransformBounds(aShadowClipRect);
  Rect transformedSkipRect = transform.TransformBounds(aSkipRect);

  transformedDestRect.Round();
  transformedShadowClipRect.Round();
  transformedSkipRect.RoundIn();

  for (auto corner : AllPhysicalCorners()) {
    aInnerClipRectRadii[corner].width =
        std::floor(scale.xScale * aInnerClipRectRadii[corner].width);
    aInnerClipRectRadii[corner].height =
        std::floor(scale.yScale * aInnerClipRectRadii[corner].height);
  }

  mGaussianBlur.BlurInsetBox(aDestinationCtx, transformedDestRect,
                             transformedShadowClipRect, blurRadius,
                             aShadowColor,
                             aHasBorderRadius ? &aInnerClipRectRadii : nullptr,
                             transformedSkipRect, aShadowOffset);
  return true;
}
