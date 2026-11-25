/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsMathMLContainerFrame.h"

#include "gfxContext.h"
#include "gfxUtils.h"
#include "mozilla/Likely.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/StaticPrefs_mathml.h"
#include "mozilla/dom/MathMLElement.h"
#include "mozilla/gfx/2D.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsGkAtoms.h"
#include "nsIScriptError.h"
#include "nsLayoutUtils.h"
#include "nsNameSpaceManager.h"
#include "nsPresContext.h"

using namespace mozilla;
using namespace mozilla::gfx;

//
// nsMathMLContainerFrame implementation
//

NS_QUERYFRAME_HEAD(nsMathMLContainerFrame)
  NS_QUERYFRAME_ENTRY(nsIMathMLFrame)
  NS_QUERYFRAME_ENTRY(nsMathMLContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

/* /////////////
 * nsIMathMLFrame - support methods for stretchy elements
 * =============================================================================
 */

static bool IsForeignChild(const nsIFrame* aFrame) {
  // This counts nsMathMLmathBlockFrame as a foreign child, because it
  // uses block reflow
  return !aFrame->IsMathMLFrame() || aFrame->IsBlockFrame();
}

NS_DECLARE_FRAME_PROPERTY_DELETABLE(HTMLReflowOutputProperty, ReflowOutput)

/* static */
void nsMathMLContainerFrame::SaveReflowAndBoundingMetricsFor(
    nsIFrame* aFrame, const ReflowOutput& aReflowOutput,
    const nsBoundingMetrics& aBoundingMetrics) {
  ReflowOutput* reflowOutput = new ReflowOutput(aReflowOutput);
  reflowOutput->mBoundingMetrics = aBoundingMetrics;
  aFrame->SetProperty(HTMLReflowOutputProperty(), reflowOutput);
}

// helper method to facilitate getting the reflow and bounding metrics
/* static */
void nsMathMLContainerFrame::GetReflowAndBoundingMetricsFor(
    nsIFrame* aFrame, ReflowOutput& aReflowOutput,
    nsBoundingMetrics& aBoundingMetrics, MathMLFrameType* aMathMLFrameType) {
  MOZ_ASSERT(aFrame, "null arg");

  ReflowOutput* reflowOutput = aFrame->GetProperty(HTMLReflowOutputProperty());

  // IMPORTANT: This function is only meant to be called in Place() methods
  // where it is assumed that SaveReflowAndBoundingMetricsFor has recorded the
  // information.
  NS_ASSERTION(reflowOutput, "Didn't SaveReflowAndBoundingMetricsFor frame!");
  if (reflowOutput) {
    aReflowOutput = *reflowOutput;
    aBoundingMetrics = reflowOutput->mBoundingMetrics;
  }

  if (aMathMLFrameType) {
    if (!IsForeignChild(aFrame)) {
      nsIMathMLFrame* mathMLFrame = do_QueryFrame(aFrame);
      if (mathMLFrame) {
        *aMathMLFrameType = mathMLFrame->GetMathMLFrameType();
        return;
      }
    }
    *aMathMLFrameType = MathMLFrameType::Unknown;
  }
}

void nsMathMLContainerFrame::ClearSavedChildMetrics() {
  nsIFrame* childFrame = mFrames.FirstChild();
  while (childFrame) {
    childFrame->RemoveProperty(HTMLReflowOutputProperty());
    childFrame = childFrame->GetNextSibling();
  }
}

nsMargin nsMathMLContainerFrame::GetBorderPaddingForPlace(
    const PlaceFlags& aFlags) {
  if (aFlags.contains(PlaceFlag::IgnoreBorderPadding)) {
    return nsMargin();
  }

  if (aFlags.contains(PlaceFlag::IntrinsicSize)) {
    // Bug 1910859: Should we provide separate left and right border/padding?
    return nsMargin(0, IntrinsicISizeOffsets().BorderPadding(), 0, 0);
  }

  return GetUsedBorderAndPadding();
}

/* static */
nsMargin nsMathMLContainerFrame::GetMarginForPlace(const PlaceFlags& aFlags,
                                                   nsIFrame* aChild) {
  if (aFlags.contains(PlaceFlag::IntrinsicSize)) {
    // Bug 1910859: Should we provide separate left and right margin?
    return nsMargin(0, aChild->IntrinsicISizeOffsets().margin, 0, 0);
  }

  return aChild->GetUsedMargin();
}

void nsMathMLContainerFrame::InflateReflowAndBoundingMetrics(
    const nsMargin& aBorderPadding, ReflowOutput& aReflowOutput,
    nsBoundingMetrics& aBoundingMetrics) {
  // Bug 1910858: It is not really clear what is the right way to update the
  // ink bounding box when adding border or padding. Below, we assume that
  // border/padding inflate it.
  aBoundingMetrics.rightBearing += aBorderPadding.LeftRight();
  aBoundingMetrics.width += aBorderPadding.LeftRight();
  aReflowOutput.mBoundingMetrics = aBoundingMetrics;
  aReflowOutput.Width() += aBorderPadding.LeftRight();
  aReflowOutput.SetBlockStartAscent(aReflowOutput.BlockStartAscent() +
                                    aBorderPadding.top);
  aReflowOutput.Height() += aBorderPadding.TopBottom();
}

nsMathMLContainerFrame::WidthAndHeightForPlaceAdjustment
nsMathMLContainerFrame::GetWidthAndHeightForPlaceAdjustment(
    const PlaceFlags& aFlags) {
  WidthAndHeightForPlaceAdjustment sizes;
  if (aFlags.contains(PlaceFlag::DoNotAdjustForWidthAndHeight)) {
    return sizes;
  }
  const nsStylePosition* stylePos = StylePosition();
  const auto& width = stylePos->mWidth;
  // TODO: Resolve percentages.
  // https://github.com/w3c/mathml-core/issues/76
  if (width.ConvertsToLength()) {
    sizes.width = Some(width.ToLength());
  }
  if (!aFlags.contains(PlaceFlag::IntrinsicSize)) {
    // TODO: Resolve percentages.
    // https://github.com/w3c/mathml-core/issues/77
    const auto& height = stylePos->mHeight;
    if (height.ConvertsToLength()) {
      sizes.height = Some(height.ToLength());
    }
  }
  return sizes;
}

nscoord nsMathMLContainerFrame::ApplyAdjustmentForWidthAndHeight(
    const PlaceFlags& aFlags, const WidthAndHeightForPlaceAdjustment& aSizes,
    ReflowOutput& aReflowOutput, nsBoundingMetrics& aBoundingMetrics) {
  nscoord shiftX = 0;
  if (aSizes.width) {
    MOZ_ASSERT(!aFlags.contains(PlaceFlag::DoNotAdjustForWidthAndHeight));
    auto width = *aSizes.width;
    auto oldWidth = aReflowOutput.Width();
    if (IsMathContentBoxHorizontallyCentered()) {
      shiftX = (width - oldWidth) / 2;
    } else if (GetWritingMode().IsBidiRTL()) {
      shiftX = width - oldWidth;
    }
    aBoundingMetrics.leftBearing = 0;
    aBoundingMetrics.rightBearing = width;
    aBoundingMetrics.width = width;
    aReflowOutput.mBoundingMetrics = aBoundingMetrics;
    aReflowOutput.Width() = width;
  }
  if (aSizes.height) {
    MOZ_ASSERT(!aFlags.contains(PlaceFlag::DoNotAdjustForWidthAndHeight));
    MOZ_ASSERT(!aFlags.contains(PlaceFlag::IntrinsicSize));
    auto height = *aSizes.height;
    aReflowOutput.Height() = height;
  }
  return shiftX;
}

// helper to get the preferred size that a container frame should use to fire
// the stretch on its stretchy child frames.
void nsMathMLContainerFrame::GetPreferredStretchSize(
    DrawTarget* aDrawTarget, PreferredStretchSizeMode aMode,
    StretchDirection aStretchDirection,
    nsBoundingMetrics& aPreferredStretchSize) {
  switch (aMode) {
    case PreferredStretchSizeMode::Embellishments: {
      // compute our up-to-date size using Place(), without border/padding.
      ReflowOutput reflowOutput(GetWritingMode());
      PlaceFlags flags(PlaceFlag::MeasureOnly, PlaceFlag::IgnoreBorderPadding);
      Place(aDrawTarget, flags, reflowOutput);
      aPreferredStretchSize = reflowOutput.mBoundingMetrics;
    } break;
    case PreferredStretchSizeMode::EmbellishmentsIfSameStretchDirection: {
      // compute a size that includes embellishments iff the container stretches
      // in the same direction as the embellished operator.
      bool stretchAll = mPresentationData.flags.contains(
          aStretchDirection == StretchDirection::Vertical
              ? MathMLPresentationFlag::StretchAllChildrenVertically
              : MathMLPresentationFlag::StretchAllChildrenHorizontally);
      NS_ASSERTION(aStretchDirection == StretchDirection::Horizontal ||
                       aStretchDirection == StretchDirection::Vertical,
                   "You must specify a direction in which to stretch");
      NS_ASSERTION(mEmbellishData.flags.contains(
                       MathMLEmbellishFlag::EmbellishedOperator) ||
                       stretchAll,
                   "invalid call to GetPreferredStretchSize");
      bool firstTime = true;
      nsBoundingMetrics bm, bmChild;
      nsIFrame* childFrame = stretchAll ? PrincipalChildList().FirstChild()
                                        : mPresentationData.baseFrame;
      while (childFrame) {
        // initializations in case this child happens not to be a MathML frame
        nsIMathMLFrame* mathMLFrame = do_QueryFrame(childFrame);
        if (mathMLFrame) {
          nsEmbellishData embellishData;
          nsPresentationData presentationData;
          mathMLFrame->GetEmbellishData(embellishData);
          mathMLFrame->GetPresentationData(presentationData);
          if (embellishData.flags.contains(
                  MathMLEmbellishFlag::EmbellishedOperator) &&
              embellishData.direction == aStretchDirection &&
              presentationData.baseFrame) {
            // embellishements are not included, only consider the inner first
            // child itself
            // XXXkt Does that mean the core descendent frame should be used
            // instead of the base child?
            nsIMathMLFrame* mathMLchildFrame =
                do_QueryFrame(presentationData.baseFrame);
            if (mathMLchildFrame) {
              mathMLFrame = mathMLchildFrame;
            }
          }
          mathMLFrame->GetBoundingMetrics(bmChild);
        } else {
          ReflowOutput unused(GetWritingMode());
          GetReflowAndBoundingMetricsFor(childFrame, unused, bmChild);
        }

        if (firstTime) {
          firstTime = false;
          bm = bmChild;
          if (!stretchAll) {
            // we may get here for cases such as <msup><mo>...</mo> ... </msup>,
            // or <maction>...<mo>...</mo></maction>.
            break;
          }
        } else {
          if (aStretchDirection == StretchDirection::Horizontal) {
            // if we get here, it means this is container that will stack its
            // children vertically and fire an horizontal stretch on each them.
            // This is the case for \munder, \mover, \munderover. We just sum-up
            // the size vertically.
            bm.descent += bmChild.ascent + bmChild.descent;
            // Sometimes non-spacing marks (when width is zero) are positioned
            // to the left of the origin, but it is the distance between left
            // and right bearing that is important rather than the offsets from
            // the origin.
            if (bmChild.width == 0) {
              bmChild.rightBearing -= bmChild.leftBearing;
              bmChild.leftBearing = 0;
            }
            if (bm.leftBearing > bmChild.leftBearing) {
              bm.leftBearing = bmChild.leftBearing;
            }
            if (bm.rightBearing < bmChild.rightBearing) {
              bm.rightBearing = bmChild.rightBearing;
            }
          } else if (aStretchDirection == StretchDirection::Vertical) {
            // just sum-up the sizes horizontally.
            bm += bmChild;
          } else {
            NS_ERROR("unexpected case in GetPreferredStretchSize");
            break;
          }
        }
        childFrame = childFrame->GetNextSibling();
      }
      aPreferredStretchSize = bm;
    } break;
  }
}

NS_IMETHODIMP
nsMathMLContainerFrame::Stretch(DrawTarget* aDrawTarget,
                                StretchDirection aStretchDirection,
                                nsBoundingMetrics& aContainerSize,
                                ReflowOutput& aDesiredStretchSize) {
  if (!mEmbellishData.flags.contains(
          MathMLEmbellishFlag::EmbellishedOperator)) {
    return NS_OK;
  }
  if (mPresentationData.flags.contains(MathMLPresentationFlag::StretchDone)) {
    NS_WARNING("it is wrong to fire stretch more than once on a frame");
    return NS_OK;
  }
  mPresentationData.flags += MathMLPresentationFlag::StretchDone;

  // Pass the stretch to the base child ...
  nsIFrame* baseFrame = mPresentationData.baseFrame;
  if (!baseFrame) {
    return NS_OK;
  }
  nsIMathMLFrame* mathMLFrame = do_QueryFrame(baseFrame);
  NS_ASSERTION(mathMLFrame, "Something is wrong somewhere");
  if (!mathMLFrame) {
    return NS_OK;
  }
  // And the trick is that the child's rect.x is still holding the
  // descent, and rect.y is still holding the ascent ...
  ReflowOutput childSize(aDesiredStretchSize);
  GetReflowAndBoundingMetricsFor(baseFrame, childSize,
                                 childSize.mBoundingMetrics);

  // See if we should downsize and confine the stretch to us...
  // XXX there may be other cases where we can downsize the stretch,
  // e.g., the first &Sum; might appear big in the following situation
  // <math xmlns='http://www.w3.org/1998/Math/MathML'>
  //   <mstyle>
  //     <msub>
  //        <msub><mo>&Sum;</mo><mfrac><mi>a</mi><mi>b</mi></mfrac></msub>
  //        <msub><mo>&Sum;</mo><mfrac><mi>a</mi><mi>b</mi></mfrac></msub>
  //      </msub>
  //   </mstyle>
  // </math>
  nsBoundingMetrics containerSize = aContainerSize;
  if (aStretchDirection != mEmbellishData.direction &&
      mEmbellishData.direction != StretchDirection::Unsupported) {
    NS_ASSERTION(mEmbellishData.direction != StretchDirection::Default,
                 "Stretches may have a default direction, operators can not.");
    if (mPresentationData.flags.contains(
            mEmbellishData.direction == StretchDirection::Vertical
                ? MathMLPresentationFlag::StretchAllChildrenVertically
                : MathMLPresentationFlag::StretchAllChildrenHorizontally)) {
      GetPreferredStretchSize(
          aDrawTarget,
          PreferredStretchSizeMode::EmbellishmentsIfSameStretchDirection,
          mEmbellishData.direction, containerSize);
      // Stop further recalculations
      aStretchDirection = mEmbellishData.direction;
    } else {
      // We aren't going to stretch the child, so just use the child
      // metrics.
      containerSize = childSize.mBoundingMetrics;
    }
  }

  // do the stretching...
  mathMLFrame->Stretch(aDrawTarget, aStretchDirection, containerSize,
                       childSize);
  // store the updated metrics
  SaveReflowAndBoundingMetricsFor(baseFrame, childSize,
                                  childSize.mBoundingMetrics);

  // Remember the siblings which were _deferred_.
  // Now that this embellished child may have changed, we need to
  // fire the stretch on its siblings using our updated size

  if (mPresentationData.flags.contains(
          MathMLPresentationFlag::StretchAllChildrenVertically) ||
      mPresentationData.flags.contains(
          MathMLPresentationFlag::StretchAllChildrenHorizontally)) {
    StretchDirection stretchDir =
        mPresentationData.flags.contains(
            MathMLPresentationFlag::StretchAllChildrenVertically)
            ? StretchDirection::Vertical
            : StretchDirection::Horizontal;

    GetPreferredStretchSize(aDrawTarget,
                            PreferredStretchSizeMode::Embellishments,
                            stretchDir, containerSize);

    nsIFrame* childFrame = mFrames.FirstChild();
    while (childFrame) {
      if (childFrame != mPresentationData.baseFrame) {
        mathMLFrame = do_QueryFrame(childFrame);
        if (mathMLFrame) {
          // retrieve the metrics that was stored at the previous pass
          GetReflowAndBoundingMetricsFor(childFrame, childSize,
                                         childSize.mBoundingMetrics);
          // do the stretching...
          mathMLFrame->Stretch(aDrawTarget, stretchDir, containerSize,
                               childSize);
          // store the updated metrics
          SaveReflowAndBoundingMetricsFor(childFrame, childSize,
                                          childSize.mBoundingMetrics);
        }
      }
      childFrame = childFrame->GetNextSibling();
    }
  }

  // re-position all our children
  PlaceFlags flags;
  Place(aDrawTarget, flags, aDesiredStretchSize);

  // If our parent is not embellished, it means we are the outermost
  // embellished container and so we put the spacing, otherwise we don't
  // include the spacing, the outermost embellished container will take
  // care of it.

  nsEmbellishData parentData;
  GetEmbellishDataFrom(GetParent(), parentData);
  // ensure that we are the embellished child, not just a sibling
  // (need to test coreFrame since <mfrac> resets other things)
  if (parentData.coreFrame != mEmbellishData.coreFrame) {
    // (we fetch values from the core since they may use units that depend
    // on style data, and style changes could have occurred in the core
    // since our last visit there)
    nsEmbellishData coreData;
    GetEmbellishDataFrom(mEmbellishData.coreFrame, coreData);

    nscoord leadingSpace = 0, trailingSpace = 0;
    if (!StaticPrefs::
            mathml_lspace_rspace_for_child_spacing_during_mrow_layout_enabled()) {
      leadingSpace = coreData.leadingSpace;
      trailingSpace = coreData.trailingSpace;
    }
    mBoundingMetrics.width += leadingSpace + trailingSpace;
    aDesiredStretchSize.Width() = mBoundingMetrics.width;
    aDesiredStretchSize.mBoundingMetrics.width = mBoundingMetrics.width;

    nscoord dx = GetWritingMode().IsBidiRTL() ? trailingSpace : leadingSpace;
    if (dx != 0) {
      mBoundingMetrics.leftBearing += dx;
      mBoundingMetrics.rightBearing += dx;
      aDesiredStretchSize.mBoundingMetrics.leftBearing += dx;
      aDesiredStretchSize.mBoundingMetrics.rightBearing += dx;

      nsIFrame* childFrame = mFrames.FirstChild();
      while (childFrame) {
        childFrame->SetPosition(childFrame->GetPosition() + nsPoint(dx, 0));
        childFrame = childFrame->GetNextSibling();
      }
    }
  }

  // Finished with these:
  ClearSavedChildMetrics();
  // Set our overflow area
  GatherAndStoreOverflow(&aDesiredStretchSize);
  return NS_OK;
}

nsresult nsMathMLContainerFrame::FinalizeReflow(DrawTarget* aDrawTarget,
                                                ReflowOutput& aDesiredSize) {
  // During reflow, we use rect.x and rect.y as placeholders for the child's
  // ascent and descent in expectation of a stretch command. Hence we need to
  // ensure that a stretch command will actually be fired later on, after
  // exiting from our reflow. If the stretch is not fired, the rect.x, and
  // rect.y will remain with inappropriate data causing children to be
  // improperly positioned. This helper method checks to see if our parent will
  // fire a stretch command targeted at us. If not, we go ahead and fire an
  // involutive stretch on ourselves. This will clear all the rect.x and rect.y,
  // and return our desired size.

  // First, complete the post-reflow hook.
  // We use the information in our children rectangles to position them.
  // If placeOrigin==false, then Place() will not touch rect.x, and rect.y.
  // They will still be holding the ascent and descent for each child.

  // The first clause caters for any non-embellished container.
  // The second clause is for a container which won't fire stretch even though
  // it is embellished, e.g., as in <mfrac><mo>...</mo> ... </mfrac>, the test
  // is convoluted because it excludes the particular case of the core
  // <mo>...</mo> itself.
  // (<mo> needs to fire stretch on its MathMLChar in any case to initialize it)
  bool placeOrigin =
      !mEmbellishData.flags.contains(
          MathMLEmbellishFlag::EmbellishedOperator) ||
      (mEmbellishData.coreFrame != this && !mPresentationData.baseFrame &&
       mEmbellishData.direction == StretchDirection::Unsupported);
  PlaceFlags flags;
  if (!placeOrigin) {
    flags += PlaceFlag::MeasureOnly;
  }
  Place(aDrawTarget, flags, aDesiredSize);

  bool parentWillFireStretch = false;
  if (!placeOrigin) {
    // This means the rect.x and rect.y of our children were not set!!
    // Don't go without checking to see if our parent will later fire a
    // Stretch() command targeted at us. The Stretch() will cause the rect.x and
    // rect.y to clear...
    nsIMathMLFrame* mathMLFrame = do_QueryFrame(GetParent());
    if (mathMLFrame) {
      nsEmbellishData embellishData;
      nsPresentationData presentationData;
      mathMLFrame->GetEmbellishData(embellishData);
      mathMLFrame->GetPresentationData(presentationData);
      if (presentationData.flags.contains(
              MathMLPresentationFlag::StretchAllChildrenVertically) ||
          presentationData.flags.contains(
              MathMLPresentationFlag::StretchAllChildrenHorizontally) ||
          (embellishData.flags.contains(
               MathMLEmbellishFlag::EmbellishedOperator) &&
           presentationData.baseFrame == this)) {
        parentWillFireStretch = true;
      }
    }
    if (!parentWillFireStretch) {
      // There is nobody who will fire the stretch for us, we do it ourselves!

      bool stretchAll =
          /* mPresentationData.flags.contains(MathMLPresentationFlag::StretchAllChildrenVertically)
             || */
          mPresentationData.flags.contains(
              MathMLPresentationFlag::StretchAllChildrenHorizontally);

      StretchDirection stretchDir;
      if (mEmbellishData.coreFrame ==
              this || /* case of a bare <mo>...</mo> itself */
          (mEmbellishData.direction == StretchDirection::Horizontal &&
           stretchAll) || /* or <mover><mo>...</mo>...</mover>, or friends */
          mEmbellishData.direction ==
              StretchDirection::Unsupported) { /* Doesn't stretch */
        stretchDir = mEmbellishData.direction;
      } else {
        // Let the Stretch() call decide the direction.
        stretchDir = StretchDirection::Default;
      }
      // Use our current size as computed earlier by Place()
      // The stretch call will detect if this is incorrect and recalculate the
      // size.
      nsBoundingMetrics defaultSize = aDesiredSize.mBoundingMetrics;

      Stretch(aDrawTarget, stretchDir, defaultSize, aDesiredSize);
#ifdef DEBUG
      {
        // The Place() call above didn't request FinishReflowChild(),
        // so let's check that we eventually did through Stretch().
        for (nsIFrame* childFrame : PrincipalChildList()) {
          NS_ASSERTION(!childFrame->HasAnyStateBits(NS_FRAME_IN_REFLOW),
                       "DidReflow() was never called");
        }
      }
#endif
    }
  }

  // Also return our bounding metrics
  aDesiredSize.mBoundingMetrics = mBoundingMetrics;

  // see if we should fix the spacing
  FixInterFrameSpacing(aDesiredSize);

  if (!parentWillFireStretch) {
    // Not expecting a stretch.
    // Finished with these:
    ClearSavedChildMetrics();
    // Set our overflow area.
    GatherAndStoreOverflow(&aDesiredSize);
  }

  mPresentationData.flags -= MathMLPresentationFlag::StretchDone;
  return NS_OK;
}

/* /////////////
 * nsIMathMLFrame - support methods for scripting elements (nested frames
 * within msub, msup, msubsup, munder, mover, munderover, mmultiscripts,
 * mfrac, mroot, mtable).
 * =============================================================================
 */

// helper to let the update of presentation data pass through
// a subtree that may contain non-mathml container frames
/* static */
void nsMathMLContainerFrame::PropagatePresentationDataFor(
    nsIFrame* aFrame, MathMLPresentationFlags aFlagsValues,
    MathMLPresentationFlags aFlagsToUpdate) {
  if (!aFrame || aFlagsToUpdate.isEmpty()) {
    return;
  }
  nsIMathMLFrame* mathMLFrame = do_QueryFrame(aFrame);
  if (mathMLFrame) {
    // update
    mathMLFrame->UpdatePresentationData(aFlagsValues, aFlagsToUpdate);
    // propagate using the base method to make sure that the control
    // is passed on to MathML frames that may be overloading the method
    mathMLFrame->UpdatePresentationDataFromChildAt(0, -1, aFlagsValues,
                                                   aFlagsToUpdate);
  } else {
    // propagate down the subtrees
    for (nsIFrame* childFrame : aFrame->PrincipalChildList()) {
      PropagatePresentationDataFor(childFrame, aFlagsValues, aFlagsToUpdate);
    }
  }
}

/* static */
void nsMathMLContainerFrame::PropagatePresentationDataFromChildAt(
    nsIFrame* aParentFrame, int32_t aFirstChildIndex, int32_t aLastChildIndex,
    MathMLPresentationFlags aFlagsValues,
    MathMLPresentationFlags aFlagsToUpdate) {
  if (!aParentFrame || aFlagsToUpdate.isEmpty()) {
    return;
  }
  int32_t index = 0;
  for (nsIFrame* childFrame : aParentFrame->PrincipalChildList()) {
    if ((index >= aFirstChildIndex) &&
        ((aLastChildIndex <= 0) ||
         ((aLastChildIndex > 0) && (index <= aLastChildIndex)))) {
      PropagatePresentationDataFor(childFrame, aFlagsValues, aFlagsToUpdate);
    }
    index++;
  }
}

/* //////////////////
 * Frame construction
 * =============================================================================
 */

void nsMathMLContainerFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                              const nsDisplayListSet& aLists) {
  BuildDisplayListForInline(aBuilder, aLists);
}

// Note that this method re-builds the automatic data in the children -- not
// in aParentFrame itself (except for those particular operations that the
// parent frame may do in its TransmitAutomaticData()).
/* static */
void nsMathMLContainerFrame::RebuildAutomaticDataForChildren(
    nsIFrame* aParentFrame) {
  // 1. As we descend the tree, make each child frame inherit data from
  // the parent
  // 2. As we ascend the tree, transmit any specific change that we want
  // down the subtrees
  for (nsIFrame* childFrame : aParentFrame->PrincipalChildList()) {
    nsIMathMLFrame* childMathMLFrame = do_QueryFrame(childFrame);
    if (childMathMLFrame) {
      childMathMLFrame->InheritAutomaticData(aParentFrame);
    }
    RebuildAutomaticDataForChildren(childFrame);
  }
  nsIMathMLFrame* mathMLFrame = do_QueryFrame(aParentFrame);
  if (mathMLFrame) {
    mathMLFrame->TransmitAutomaticData();
  }
}

/* static */
nsresult nsMathMLContainerFrame::ReLayoutChildren(nsIFrame* aParentFrame) {
  if (!aParentFrame) {
    return NS_OK;
  }

  // walk-up to the first frame that is a MathML frame, stop if we reach <math>
  nsIFrame* frame = aParentFrame;
  while (true) {
    nsIFrame* parent = frame->GetParent();
    if (!parent || !parent->GetContent()) {
      break;
    }

    // stop if it is a MathML frame
    nsIMathMLFrame* mathMLFrame = do_QueryFrame(frame);
    if (mathMLFrame) {
      break;
    }

    // stop if we reach the root <math> tag
    nsIContent* content = frame->GetContent();
    NS_ASSERTION(content, "dangling frame without a content node");
    if (!content) {
      break;
    }
    if (content->IsMathMLElement(nsGkAtoms::math)) {
      break;
    }

    frame = parent;
  }

  // re-sync the presentation data and embellishment data of our children
  RebuildAutomaticDataForChildren(frame);

  // Ask our parent frame to reflow us
  nsIFrame* parent = frame->GetParent();
  NS_ASSERTION(parent, "No parent to pass the reflow request up to");
  if (!parent) {
    return NS_OK;
  }

  frame->PresShell()->FrameNeedsReflow(
      frame, IntrinsicDirty::FrameAncestorsAndDescendants, NS_FRAME_IS_DIRTY);

  return NS_OK;
}

// There are precise rules governing children of a MathML frame,
// and properties such as the scriptlevel depends on those rules.
// Hence for things to work, callers must use Append/Insert/etc wisely.

nsresult nsMathMLContainerFrame::ChildListChanged() {
  // If this is an embellished frame we need to rebuild the
  // embellished hierarchy by walking-up to the parent of the
  // outermost embellished container.
  nsIFrame* frame = this;
  if (mEmbellishData.coreFrame) {
    nsIFrame* parent = GetParent();
    nsEmbellishData embellishData;
    for (; parent; frame = parent, parent = parent->GetParent()) {
      GetEmbellishDataFrom(parent, embellishData);
      if (embellishData.coreFrame != mEmbellishData.coreFrame) {
        break;
      }
    }
  }
  return ReLayoutChildren(frame);
}

void nsMathMLContainerFrame::AppendFrames(ChildListID aListID,
                                          nsFrameList&& aFrameList) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal);
  mFrames.AppendFrames(this, std::move(aFrameList));
  ChildListChanged();
}

void nsMathMLContainerFrame::InsertFrames(
    ChildListID aListID, nsIFrame* aPrevFrame,
    const nsLineList::iterator* aPrevFrameLine, nsFrameList&& aFrameList) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal);
  mFrames.InsertFrames(this, aPrevFrame, std::move(aFrameList));
  ChildListChanged();
}

void nsMathMLContainerFrame::RemoveFrame(DestroyContext& aContext,
                                         ChildListID aListID,
                                         nsIFrame* aOldFrame) {
  MOZ_ASSERT(aListID == FrameChildListID::Principal);
  mFrames.DestroyFrame(aContext, aOldFrame);
  ChildListChanged();
}

void nsMathMLContainerFrame::GatherAndStoreOverflow(ReflowOutput* aMetrics) {
  mBlockStartAscent = aMetrics->BlockStartAscent();

  // nsIFrame::FinishAndStoreOverflow likes the overflow area to include the
  // frame rectangle.
  aMetrics->SetOverflowAreasToDesiredBounds();

  ComputeCustomOverflow(aMetrics->mOverflowAreas);

  // mBoundingMetrics does not necessarily include content of <mpadded>
  // elements whose mBoundingMetrics may not be representative of the true
  // bounds, and doesn't include the CSS2 outline rectangles of children, so
  // make such to include child overflow areas.
  UnionChildOverflow(aMetrics->mOverflowAreas);

  FinishAndStoreOverflow(aMetrics);
}

bool nsMathMLContainerFrame::ComputeCustomOverflow(
    OverflowAreas& aOverflowAreas) {
  // All non-child-frame content such as nsMathMLChars (and most child-frame
  // content) is included in mBoundingMetrics.
  nsRect boundingBox(
      mBoundingMetrics.leftBearing, mBlockStartAscent - mBoundingMetrics.ascent,
      mBoundingMetrics.rightBearing - mBoundingMetrics.leftBearing,
      mBoundingMetrics.ascent + mBoundingMetrics.descent);

  // REVIEW: Maybe this should contribute only to ink overflow
  // and not scrollable?
  aOverflowAreas.UnionAllWith(boundingBox);
  return nsContainerFrame::ComputeCustomOverflow(aOverflowAreas);
}

void nsMathMLContainerFrame::ReflowChild(nsIFrame* aChildFrame,
                                         nsPresContext* aPresContext,
                                         ReflowOutput& aDesiredSize,
                                         const ReflowInput& aReflowInput,
                                         nsReflowStatus& aStatus) {
  // Having foreign/hybrid children, e.g., from html markups, is not defined by
  // the MathML spec. But it can happen in practice, e.g., <html:img> allows us
  // to do some cool demos... or we may have a child that is an nsInlineFrame
  // from a generated content such as :before { content: open-quote } or
  // :after { content: close-quote }. Unfortunately, the other frames out-there
  // may expect their own invariants that are not met when we mix things.
  // Hence we do not claim their support, but we will nevertheless attempt to
  // keep them in the flow, if we can get their desired size. We observed that
  // most frames may be reflowed generically, but nsInlineFrames need extra
  // care.

  NS_ASSERTION(!aChildFrame->IsInlineFrameOrSubclass(),
               "Inline frames should be wrapped in blocks");

  nsContainerFrame::ReflowChild(aChildFrame, aPresContext, aDesiredSize,
                                aReflowInput, 0, 0,
                                ReflowChildFlags::NoMoveFrame, aStatus);

  if (aDesiredSize.BlockStartAscent() == ReflowOutput::ASK_FOR_BASELINE) {
    // This will be suitable for inline frames, which are wrapped in a block.
    nscoord ascent;
    WritingMode wm = aDesiredSize.GetWritingMode();
    if (!nsLayoutUtils::GetLastLineBaseline(wm, aChildFrame, &ascent)) {
      // We don't expect any other block children so just place the frame on
      // the baseline instead of going through DidReflow() and
      // GetBaseline().  This is what nsIFrame::GetBaseline() will do anyway.
      aDesiredSize.SetBlockStartAscent(aDesiredSize.BSize(wm));
    } else {
      aDesiredSize.SetBlockStartAscent(ascent);
    }
  }
  if (IsForeignChild(aChildFrame)) {
    // use ComputeTightBounds API as aDesiredSize.mBoundingMetrics is not set.
    nsRect r = aChildFrame->ComputeTightBounds(
        aReflowInput.mRenderingContext->GetDrawTarget());
    aDesiredSize.mBoundingMetrics.leftBearing = r.x;
    aDesiredSize.mBoundingMetrics.rightBearing = r.XMost();
    aDesiredSize.mBoundingMetrics.ascent =
        aDesiredSize.BlockStartAscent() - r.y;
    aDesiredSize.mBoundingMetrics.descent =
        r.YMost() - aDesiredSize.BlockStartAscent();
    aDesiredSize.mBoundingMetrics.width = aDesiredSize.Width();
  }
}

void nsMathMLContainerFrame::Reflow(nsPresContext* aPresContext,
                                    ReflowOutput& aDesiredSize,
                                    const ReflowInput& aReflowInput,
                                    nsReflowStatus& aStatus) {
  if (IsHiddenByContentVisibilityOfInFlowParentForLayout()) {
    return;
  }

  MarkInReflow();
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  aDesiredSize.Width() = aDesiredSize.Height() = 0;
  aDesiredSize.SetBlockStartAscent(0);
  aDesiredSize.mBoundingMetrics = nsBoundingMetrics();

  /////////////
  // Reflow children
  // Asking each child to cache its bounding metrics

  nsReflowStatus childStatus;
  nsIFrame* childFrame = mFrames.FirstChild();
  while (childFrame) {
    ReflowOutput childDesiredSize(aReflowInput);
    WritingMode wm = childFrame->GetWritingMode();
    LogicalSize availSize = aReflowInput.ComputedSize(wm);
    availSize.BSize(wm) = NS_UNCONSTRAINEDSIZE;
    ReflowInput childReflowInput(aPresContext, aReflowInput, childFrame,
                                 availSize);
    ReflowChild(childFrame, aPresContext, childDesiredSize, childReflowInput,
                childStatus);
    // NS_ASSERTION(childStatus.IsComplete(), "bad status");
    SaveReflowAndBoundingMetricsFor(childFrame, childDesiredSize,
                                    childDesiredSize.mBoundingMetrics);
    childFrame = childFrame->GetNextSibling();
  }

  /////////////
  // If we are a container which is entitled to stretch its children, then we
  // ask our stretchy children to stretch themselves

  // The stretching of siblings of an embellished child is _deferred_ until
  // after finishing the stretching of the embellished child - bug 117652

  DrawTarget* drawTarget = aReflowInput.mRenderingContext->GetDrawTarget();

  if (!mEmbellishData.flags.contains(
          MathMLEmbellishFlag::EmbellishedOperator) &&
      (mPresentationData.flags.contains(
           MathMLPresentationFlag::StretchAllChildrenVertically) ||
       mPresentationData.flags.contains(
           MathMLPresentationFlag::StretchAllChildrenHorizontally))) {
    // get the stretchy direction
    StretchDirection stretchDir =
        mPresentationData.flags.contains(
            MathMLPresentationFlag::StretchAllChildrenVertically)
            ? StretchDirection::Vertical
            : StretchDirection::Horizontal;

    // what size should we use to stretch our stretchy children
    // We don't use PreferredStretchSizeMode::Embellishments -- because
    // we don't want to include them in the caculations of the size of stretchy
    // elements
    nsBoundingMetrics containerSize;
    GetPreferredStretchSize(
        drawTarget,
        PreferredStretchSizeMode::EmbellishmentsIfSameStretchDirection,
        stretchDir, containerSize);

    // fire the stretch on each child
    childFrame = mFrames.FirstChild();
    while (childFrame) {
      nsIMathMLFrame* mathMLFrame = do_QueryFrame(childFrame);
      if (mathMLFrame) {
        // retrieve the metrics that was stored at the previous pass
        ReflowOutput childDesiredSize(aReflowInput);
        GetReflowAndBoundingMetricsFor(childFrame, childDesiredSize,
                                       childDesiredSize.mBoundingMetrics);

        mathMLFrame->Stretch(drawTarget, stretchDir, containerSize,
                             childDesiredSize);
        // store the updated metrics
        SaveReflowAndBoundingMetricsFor(childFrame, childDesiredSize,
                                        childDesiredSize.mBoundingMetrics);
      }
      childFrame = childFrame->GetNextSibling();
    }
  }

  /////////////
  // Place children now by re-adjusting the origins to align the baselines
  FinalizeReflow(drawTarget, aDesiredSize);
}

static nscoord AddInterFrameSpacingToSize(ReflowOutput& aDesiredSize,
                                          nsMathMLContainerFrame* aFrame);

/* virtual */
void nsMathMLContainerFrame::MarkIntrinsicISizesDirty() {
  mIntrinsicISize = NS_INTRINSIC_ISIZE_UNKNOWN;
  nsContainerFrame::MarkIntrinsicISizesDirty();
}

void nsMathMLContainerFrame::UpdateIntrinsicISize(
    gfxContext* aRenderingContext) {
  if (mIntrinsicISize == NS_INTRINSIC_ISIZE_UNKNOWN) {
    ReflowOutput desiredSize(GetWritingMode());
    GetIntrinsicISizeMetrics(aRenderingContext, desiredSize);

    // Include the additional width added by FixInterFrameSpacing to ensure
    // consistent width calculations.
    AddInterFrameSpacingToSize(desiredSize, this);

    // ReflowOuput::mSize corresponds to the border box, but callers
    // expect padding/border are not included.
    mIntrinsicISize = desiredSize.ISize(GetWritingMode()) -
                      IntrinsicISizeOffsets().BorderPadding();
  }
}

nscoord nsMathMLContainerFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                               IntrinsicISizeType aType) {
  UpdateIntrinsicISize(aInput.mContext);
  return mIntrinsicISize;
}

/* virtual */
void nsMathMLContainerFrame::GetIntrinsicISizeMetrics(
    gfxContext* aRenderingContext, ReflowOutput& aDesiredSize) {
  // Get child widths
  nsIFrame* childFrame = mFrames.FirstChild();
  while (childFrame) {
    ReflowOutput childDesiredSize(GetWritingMode());  // ???

    nsMathMLContainerFrame* containerFrame = do_QueryFrame(childFrame);
    if (containerFrame) {
      containerFrame->GetIntrinsicISizeMetrics(aRenderingContext,
                                               childDesiredSize);
    } else {
      nscoord width = nsLayoutUtils::IntrinsicForContainer(
          aRenderingContext, childFrame, IntrinsicISizeType::PrefISize);

      childDesiredSize.Width() = width;
      childDesiredSize.mBoundingMetrics.width = width;
      childDesiredSize.mBoundingMetrics.leftBearing = 0;
      childDesiredSize.mBoundingMetrics.rightBearing = width;

      nscoord x, xMost;
      if (NS_SUCCEEDED(childFrame->GetPrefWidthTightBounds(aRenderingContext,
                                                           &x, &xMost))) {
        childDesiredSize.mBoundingMetrics.leftBearing = x;
        childDesiredSize.mBoundingMetrics.rightBearing = xMost;
      }
    }

    SaveReflowAndBoundingMetricsFor(childFrame, childDesiredSize,
                                    childDesiredSize.mBoundingMetrics);

    childFrame = childFrame->GetNextSibling();
  }

  // Measure
  PlaceFlags flags(PlaceFlag::IntrinsicSize, PlaceFlag::MeasureOnly);
  Place(aRenderingContext->GetDrawTarget(), flags, aDesiredSize);

  ClearSavedChildMetrics();
}

// see spacing table in Chapter 18, TeXBook (p.170)
// Our table isn't quite identical to TeX because operators have
// built-in values for lspace & rspace in the Operator Dictionary.
static constexpr uint8_t
    kInterFrameSpacingTable[MathMLFrameTypeCount][MathMLFrameTypeCount] = {
        // clang-format off
  // in units of muspace.
  // Ord OpOrd OpInv OpUsr Inner Italic Upright
  {  0,  0,    0,    1,    1,    0,     0      }, // Ord
  {  0,  0,    0,    0,    0,    0,     0      }, // OpOrd
  {  0,  0,    0,    0,    0,    0,     0      }, // OpInv
  {  1,  0,    0,    1,    1,    1,     1      }, // OpUsr
  {  1,  0,    0,    1,    1,    1,     1      }, // Inner
  {  0,  0,    0,    1,    1,    0,     1      }, // Italic
  {  0,  0,    0,    1,    1,    1,     0      }, // Upright
        // clang-format on
};

static int32_t GetInterFrameSpacing(MathMLFrameType aFirstFrameType,
                                    MathMLFrameType aSecondFrameType) {
  // no space if there is a frame that we know nothing about.
  if (aFirstFrameType == MathMLFrameType::Unknown ||
      aSecondFrameType == MathMLFrameType::Unknown) {
    return 0;
  }
  return kInterFrameSpacingTable[size_t(aFirstFrameType)]
                                [size_t(aSecondFrameType)];
};

// This function computes the inter-space between two frames. However,
// since invisible operators need special treatment, the inter-space may
// be delayed when an invisible operator is encountered. In this case,
// the function will carry the inter-space forward until it is determined
// that it can be applied properly (i.e., until we encounter a visible
// frame where to decide whether to accept or reject the inter-space).
// aFromFrameType: remembers the frame when the carry-forward initiated.
// aCarrySpace: keeps track of the inter-space that is delayed.
// @returns: current inter-space (which is 0 when the true inter-space is
// delayed -- and thus has no effect since the frame is invisible anyway).
static nscoord GetInterFrameSpacing(MathMLFrameType aFirstFrameType,
                                    MathMLFrameType aSecondFrameType,
                                    MathMLFrameType* aFromFrameType,  // IN/OUT
                                    int32_t* aCarrySpace)             // IN/OUT
{
  MathMLFrameType firstType = aFirstFrameType;
  MathMLFrameType secondType = aSecondFrameType;

  int32_t space = GetInterFrameSpacing(firstType, secondType);

  // feedback control to avoid the inter-space to be added when not necessary
  if (secondType == MathMLFrameType::OperatorInvisible) {
    // see if we should start to carry the space forward until we
    // encounter a visible frame
    if (*aFromFrameType == MathMLFrameType::Unknown) {
      *aFromFrameType = firstType;
      *aCarrySpace = space;
    }
    // keep carrying *aCarrySpace forward, while returning 0 for this stage
    space = 0;
  } else if (*aFromFrameType != MathMLFrameType::Unknown) {
    // no carry-forward anymore, get the real inter-space between
    // the two frames of interest

    firstType = *aFromFrameType;

    // But... the invisible operator that we encountered earlier could
    // be sitting between italic and upright identifiers, e.g.,
    //
    // 1. <mi>sin</mi> <mo>&ApplyFunction;</mo> <mi>x</mi>
    // 2. <mi>x</mi> <mo>&InvisibileTime;</mo> <mi>sin</mi>
    //
    // the trick to get the inter-space in either situation
    // is to promote "<mi>sin</mi><mo>&ApplyFunction;</mo>" and
    // "<mo>&InvisibileTime;</mo><mi>sin</mi>" to user-defined operators...
    if (firstType == MathMLFrameType::UprightIdentifier) {
      firstType = MathMLFrameType::OperatorUserDefined;
    } else if (secondType == MathMLFrameType::UprightIdentifier) {
      secondType = MathMLFrameType::OperatorUserDefined;
    }

    space = GetInterFrameSpacing(firstType, secondType);

    // Now, we have two values: the computed space and the space that
    // has been carried forward until now. Which value do we pick?
    // If the second type is an operator (e.g., fence), it already has
    // built-in lspace & rspace, so we let them win. Otherwise we pick
    // the max between the two values that we have.
    if (secondType != MathMLFrameType::OperatorOrdinary &&
        space < *aCarrySpace) {
      space = *aCarrySpace;
    }

    // reset everything now that the carry-forward is done
    *aFromFrameType = MathMLFrameType::Unknown;
    *aCarrySpace = 0;
  }

  return space;
}

static nscoord GetThinSpace(const nsStyleFont* aStyleFont) {
  return aStyleFont->mFont.size.ScaledBy(3.0f / 18.0f).ToAppUnits();
}

static void GetCoreOperatorLeftAndRightSpace(nsIFrame* aFrame, bool aRTL,
                                             nscoord& aLeftSpace,
                                             nscoord& aRightSpace) {
  if (!StaticPrefs::
          mathml_lspace_rspace_for_child_spacing_during_mrow_layout_enabled()) {
    aLeftSpace = 0;
    aRightSpace = 0;
    return;
  }

  nsEmbellishData embellishData;
  nsMathMLContainerFrame::GetEmbellishDataFrom(aFrame, embellishData);
  nsEmbellishData coreData;
  nsMathMLContainerFrame::GetEmbellishDataFrom(embellishData.coreFrame,
                                               coreData);
  aLeftSpace = aRTL ? coreData.trailingSpace : coreData.leadingSpace;
  aRightSpace = aRTL ? coreData.leadingSpace : coreData.trailingSpace;
}

class nsMathMLContainerFrame::RowChildFrameIterator {
 public:
  explicit RowChildFrameIterator(nsMathMLContainerFrame* aParentFrame,
                                 const PlaceFlags& aFlags,
                                 bool aAddOperatorSpacing)
      : mParentFrame(aParentFrame),
        mReflowOutput(aParentFrame->GetWritingMode()),
        mX(0),
        mFlags(aFlags),
        mAddOperatorSpacing(aAddOperatorSpacing),
        mChildFrameType(MathMLFrameType::Unknown),
        mCarrySpace(0),
        mFromFrameType(MathMLFrameType::Unknown),
        mRTL(aParentFrame->GetWritingMode().IsBidiRTL()) {
    if (!mRTL) {
      mChildFrame = aParentFrame->mFrames.FirstChild();
    } else {
      mChildFrame = aParentFrame->mFrames.LastChild();
    }

    if (!mChildFrame) {
      return;
    }

    InitMetricsForChild();
  }

  RowChildFrameIterator& operator++() {
    // add child size + italic correction
    mX += mReflowOutput.mBoundingMetrics.width + mItalicCorrection;
    mX += mMargin.LeftRight();

    if (mAddOperatorSpacing) {
      nscoord dummy, rightSpace;
      GetCoreOperatorLeftAndRightSpace(mChildFrame, mRTL, dummy, rightSpace);
      mX += rightSpace;
    }

    if (!mRTL) {
      mChildFrame = mChildFrame->GetNextSibling();
    } else {
      mChildFrame = mChildFrame->GetPrevSibling();
    }

    if (!mChildFrame) {
      return *this;
    }

    MathMLFrameType prevFrameType = mChildFrameType;
    InitMetricsForChild();

    // add inter frame spacing
    nscoord space = GetInterFrameSpacing(prevFrameType, mChildFrameType,
                                         &mFromFrameType, &mCarrySpace);
    mX += space * GetThinSpace(mParentFrame->StyleFont());

    return *this;
  }

  nsIFrame* Frame() const { return mChildFrame; }
  nscoord X() const { return mX; }
  const ReflowOutput& GetReflowOutput() const { return mReflowOutput; }
  nscoord Ascent() const { return mReflowOutput.BlockStartAscent(); }
  nscoord Descent() const {
    return mReflowOutput.Height() - mReflowOutput.BlockStartAscent();
  }
  const nsMargin& Margin() const { return mMargin; }
  const nsBoundingMetrics& BoundingMetrics() const {
    return mReflowOutput.mBoundingMetrics;
  }

 private:
  const nsMathMLContainerFrame* mParentFrame;
  nsIFrame* mChildFrame;
  ReflowOutput mReflowOutput;
  nscoord mX;
  const PlaceFlags mFlags;
  bool mAddOperatorSpacing;
  nsMargin mMargin;

  nscoord mItalicCorrection = 0;
  MathMLFrameType mChildFrameType;
  int32_t mCarrySpace;
  MathMLFrameType mFromFrameType;

  bool mRTL;

  void InitMetricsForChild() {
    if (mAddOperatorSpacing) {
      nscoord leftSpace, dummy;
      GetCoreOperatorLeftAndRightSpace(mChildFrame, mRTL, leftSpace, dummy);
      mX += leftSpace;
    }

    GetReflowAndBoundingMetricsFor(mChildFrame, mReflowOutput,
                                   mReflowOutput.mBoundingMetrics,
                                   &mChildFrameType);
    mMargin = GetMarginForPlace(mFlags, mChildFrame);
    nscoord leftCorrection, rightCorrection;
    GetItalicCorrection(mReflowOutput.mBoundingMetrics, leftCorrection,
                        rightCorrection);
    if (!mChildFrame->GetPrevSibling() &&
        mParentFrame->GetContent()->IsMathMLElement(nsGkAtoms::msqrt)) {
      // Remove leading correction in <msqrt> because the sqrt glyph itself is
      // there first.
      if (!mRTL) {
        leftCorrection = 0;
      } else {
        rightCorrection = 0;
      }
    }
    // add left correction -- this fixes the problem of the italic 'f'
    // e.g., <mo>q</mo> <mi>f</mi> <mo>I</mo>
    mX += leftCorrection;
    mItalicCorrection = rightCorrection;
  }
};

/* virtual */
void nsMathMLContainerFrame::Place(DrawTarget* aDrawTarget,
                                   const PlaceFlags& aFlags,
                                   ReflowOutput& aDesiredSize) {
  // This is needed in case this frame is empty (i.e., no child frames)
  mBoundingMetrics = nsBoundingMetrics();

  // TODO(bug 2018403): Also add space around children of mtd/math elements,
  // these are currently handled specially in AddInterFrameSpacingToSize().
  bool add_space =
      !mEmbellishData.flags.contains(MathMLEmbellishFlag::EmbellishedOperator);
  RowChildFrameIterator child(this, aFlags, add_space);
  nscoord ascent = 0, descent = 0;
  while (child.Frame()) {
    nscoord topMargin = child.Margin().top;
    nscoord bottomMargin = child.Margin().bottom;
    ascent = std::max(ascent, child.Ascent() + topMargin);
    descent = std::max(descent, child.Descent() + bottomMargin);

    // add the child size
    mBoundingMetrics.width = child.X();
    nsBoundingMetrics childBm = child.BoundingMetrics();
    childBm.ascent += topMargin;
    childBm.descent += bottomMargin;
    childBm.rightBearing += child.Margin().LeftRight();
    childBm.width += child.Margin().LeftRight();
    mBoundingMetrics += childBm;

    ++child;
  }

  // Add the italic correction at the end (including the last child).
  // This gives a nice gap between math and non-math frames, and still
  // gives the same math inter-spacing in case this frame connects to
  // another math frame
  mBoundingMetrics.width = child.X();

  aDesiredSize.Width() = std::max(0, mBoundingMetrics.width);
  aDesiredSize.Height() = ascent + descent;
  aDesiredSize.SetBlockStartAscent(ascent);
  aDesiredSize.mBoundingMetrics = mBoundingMetrics;

  // Apply inline/block sizes to math content box.
  auto sizes = GetWidthAndHeightForPlaceAdjustment(aFlags);
  nscoord shiftX = ApplyAdjustmentForWidthAndHeight(aFlags, sizes, aDesiredSize,
                                                    mBoundingMetrics);

  // Add padding+border.
  auto borderPadding = GetBorderPaddingForPlace(aFlags);
  InflateReflowAndBoundingMetrics(borderPadding, aDesiredSize,
                                  mBoundingMetrics);
  shiftX += borderPadding.left;

  mReference.x = 0;
  mReference.y = aDesiredSize.BlockStartAscent();

  //////////////////
  // Place Children
  if (!aFlags.contains(PlaceFlag::MeasureOnly)) {
    PositionRowChildFrames(shiftX, aDesiredSize.BlockStartAscent(), add_space);
  }
}

void nsMathMLContainerFrame::PlaceAsMrow(DrawTarget* aDrawTarget,
                                         const PlaceFlags& aFlags,
                                         ReflowOutput& aDesiredSize) {
  nsMathMLContainerFrame::Place(aDrawTarget, aFlags, aDesiredSize);
}

void nsMathMLContainerFrame::PositionRowChildFrames(nscoord aOffsetX,
                                                    nscoord aBaseline,
                                                    bool aAddOperatorSpacing) {
  PlaceFlags flags;
  RowChildFrameIterator child(this, flags, aAddOperatorSpacing);
  while (child.Frame()) {
    nscoord dx = aOffsetX + child.X() + child.Margin().left;
    nscoord dy = aBaseline - child.Ascent();
    FinishReflowChild(child.Frame(), PresContext(), child.GetReflowOutput(),
                      nullptr, dx, dy, ReflowChildFlags::Default);
    ++child;
  }
}

// helpers to fix the inter-spacing when <math> is the only parent
// e.g., it fixes <math> <mi>f</mi> <mo>q</mo> <mi>f</mi> <mo>I</mo> </math>

static nscoord GetInterFrameSpacingFor(nsIFrame* aParentFrame,
                                       nsIFrame* aChildFrame) {
  nsIFrame* childFrame = aParentFrame->PrincipalChildList().FirstChild();
  if (!childFrame || aChildFrame == childFrame) {
    return 0;
  }

  int32_t carrySpace = 0;
  MathMLFrameType fromFrameType = MathMLFrameType::Unknown;
  MathMLFrameType prevFrameType = MathMLFrameType::Unknown;
  MathMLFrameType childFrameType =
      nsMathMLFrame::GetMathMLFrameTypeFor(childFrame);
  childFrame = childFrame->GetNextSibling();
  while (childFrame) {
    prevFrameType = childFrameType;
    childFrameType = nsMathMLFrame::GetMathMLFrameTypeFor(childFrame);
    nscoord space = GetInterFrameSpacing(prevFrameType, childFrameType,
                                         &fromFrameType, &carrySpace);
    if (aChildFrame == childFrame) {
      // get thinspace
      ComputedStyle* parentContext = aParentFrame->Style();
      nscoord thinSpace = GetThinSpace(parentContext->StyleFont());
      // we are done
      return space * thinSpace;
    }
    childFrame = childFrame->GetNextSibling();
  }

  MOZ_ASSERT_UNREACHABLE("child not in the childlist of its parent");
  return 0;
}

static nscoord AddInterFrameSpacingToSize(ReflowOutput& aDesiredSize,
                                          nsMathMLContainerFrame* aFrame) {
  nscoord gap = 0;
  nsIFrame* parent = aFrame->GetParent();
  nsIContent* parentContent = parent->GetContent();
  if (MOZ_UNLIKELY(!parentContent)) {
    return 0;
  }
  // TODO(bug 2018403): Wrap children of math and mtd in an anonymous mrow, so
  // that this whole logic can go away.
  if (parentContent->IsAnyOfMathMLElements(nsGkAtoms::math, nsGkAtoms::mtd)) {
    gap = GetInterFrameSpacingFor(parent, aFrame);
    // add our own italic correction
    nscoord leftCorrection = 0, italicCorrection = 0;
    nsMathMLContainerFrame::GetItalicCorrection(
        aDesiredSize.mBoundingMetrics, leftCorrection, italicCorrection);
    gap += leftCorrection;

    // Take into account lspace/rspace around (embellished) operators.
    nscoord leftSpace, rightSpace;
    bool isRTL = parent->GetWritingMode().IsBidiRTL();
    GetCoreOperatorLeftAndRightSpace(aFrame, isRTL, leftSpace, rightSpace);
    gap += leftSpace;

    if (gap) {
      aDesiredSize.mBoundingMetrics.leftBearing += gap;
      aDesiredSize.mBoundingMetrics.rightBearing += gap;
      aDesiredSize.mBoundingMetrics.width += gap;
      aDesiredSize.Width() += gap;
    }
    aDesiredSize.mBoundingMetrics.width += italicCorrection + rightSpace;
    aDesiredSize.Width() += italicCorrection + rightSpace;
  }
  return gap;
}

nscoord nsMathMLContainerFrame::FixInterFrameSpacing(
    ReflowOutput& aDesiredSize) {
  nscoord gap = 0;
  gap = AddInterFrameSpacingToSize(aDesiredSize, this);
  if (gap) {
    // Shift our children to account for the correction
    nsIFrame* childFrame = mFrames.FirstChild();
    while (childFrame) {
      childFrame->SetPosition(childFrame->GetPosition() + nsPoint(gap, 0));
      childFrame = childFrame->GetNextSibling();
    }
  }
  return gap;
}

// helper used by mstyle, mphantom, mpadded and mrow in their implementations
// of TransmitAutomaticData().
nsresult nsMathMLContainerFrame::TransmitAutomaticDataForMrowLikeElement() {
  //
  // One loop to check both conditions below:
  //
  // 1) whether all the children of the mrow-like element are space-like.
  //
  //   The REC defines the following elements to be "space-like":
  //   * an mstyle, mphantom, or mpadded element, all of whose direct
  //     sub-expressions are space-like;
  //   * an mrow all of whose direct sub-expressions are space-like.
  //
  // 2) whether all but one child of the mrow-like element are space-like and
  //    this non-space-like child is an embellished operator.
  //
  //   The REC defines the following elements to be embellished operators:
  //   * one of the elements mstyle, mphantom, or mpadded, such that an mrow
  //     containing the same arguments would be an embellished operator;
  //   * an mrow whose arguments consist (in any order) of one embellished
  //     operator and zero or more space-like elements.
  //
  nsIFrame *childFrame, *baseFrame;
  bool embellishedOpFound = false;
  nsEmbellishData embellishData;

  for (childFrame = PrincipalChildList().FirstChild(); childFrame;
       childFrame = childFrame->GetNextSibling()) {
    nsIMathMLFrame* mathMLFrame = do_QueryFrame(childFrame);
    if (!mathMLFrame) {
      break;
    }
    if (!mathMLFrame->IsSpaceLike()) {
      if (embellishedOpFound) {
        break;
      }
      baseFrame = childFrame;
      GetEmbellishDataFrom(baseFrame, embellishData);
      if (!embellishData.flags.contains(
              MathMLEmbellishFlag::EmbellishedOperator)) {
        break;
      }
      embellishedOpFound = true;
    }
  }

  if (!childFrame) {
    // we successfully went to the end of the loop. This means that one of
    // condition 1) or 2) holds.
    if (!embellishedOpFound) {
      // the mrow-like element is space-like.
      mPresentationData.flags += MathMLPresentationFlag::SpaceLike;
    } else {
      // the mrow-like element is an embellished operator.
      // let the state of the embellished operator found bubble to us.
      mPresentationData.baseFrame = baseFrame;
      mEmbellishData = embellishData;
    }
  }

  if (childFrame || !embellishedOpFound) {
    // The element is not embellished operator
    mPresentationData.baseFrame = nullptr;
    mEmbellishData.flags.clear();
    mEmbellishData.coreFrame = nullptr;
    mEmbellishData.direction = StretchDirection::Unsupported;
    mEmbellishData.leadingSpace = 0;
    mEmbellishData.trailingSpace = 0;
  }

  if (childFrame || embellishedOpFound) {
    // The element is not space-like
    mPresentationData.flags -= MathMLPresentationFlag::SpaceLike;
  }

  return NS_OK;
}

/*static*/
void nsMathMLContainerFrame::PropagateFrameFlagFor(nsIFrame* aFrame,
                                                   nsFrameState aFlags) {
  if (!aFrame || !aFlags) {
    return;
  }

  aFrame->AddStateBits(aFlags);
  for (nsIFrame* childFrame : aFrame->PrincipalChildList()) {
    PropagateFrameFlagFor(childFrame, aFlags);
  }
}

nsresult nsMathMLContainerFrame::ReportErrorToConsole(
    const char* errorMsgId, const nsTArray<nsString>& aParams) {
  return nsContentUtils::ReportToConsole(
      nsIScriptError::errorFlag, "Layout: MathML"_ns, mContent->OwnerDoc(),
      PropertiesFile::MATHML_PROPERTIES, errorMsgId, aParams);
}

nsresult nsMathMLContainerFrame::ReportParseError(const char16_t* aAttribute,
                                                  const char16_t* aValue) {
  AutoTArray<nsString, 3> argv;
  argv.AppendElement(aValue);
  argv.AppendElement(aAttribute);
  argv.AppendElement(nsDependentAtomString(mContent->NodeInfo()->NameAtom()));
  return ReportErrorToConsole("AttributeParsingError", argv);
}

nsresult nsMathMLContainerFrame::ReportChildCountError() {
  AutoTArray<nsString, 1> arg = {
      nsDependentAtomString(mContent->NodeInfo()->NameAtom())};
  return ReportErrorToConsole("ChildCountIncorrect", arg);
}

nsresult nsMathMLContainerFrame::ReportInvalidChildError(nsAtom* aChildTag) {
  AutoTArray<nsString, 2> argv = {
      nsDependentAtomString(aChildTag),
      nsDependentAtomString(mContent->NodeInfo()->NameAtom())};
  return ReportErrorToConsole("InvalidChild", argv);
}

//==========================

nsContainerFrame* NS_NewMathMLmathBlockFrame(PresShell* aPresShell,
                                             ComputedStyle* aStyle) {
  auto newFrame =
      new nsMathMLmathBlockFrame(aStyle, aPresShell->GetPresContext());
  return newFrame;
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmathBlockFrame)

NS_QUERYFRAME_HEAD(nsMathMLmathBlockFrame)
  NS_QUERYFRAME_ENTRY(nsMathMLmathBlockFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsBlockFrame)

nsContainerFrame* NS_NewMathMLmathInlineFrame(PresShell* aPresShell,
                                              ComputedStyle* aStyle) {
  return new nsMathMLmathInlineFrame(aStyle, aPresShell->GetPresContext());
}

NS_IMPL_FRAMEARENA_HELPERS(nsMathMLmathInlineFrame)

NS_QUERYFRAME_HEAD(nsMathMLmathInlineFrame)
  NS_QUERYFRAME_ENTRY(nsIMathMLFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsInlineFrame)
