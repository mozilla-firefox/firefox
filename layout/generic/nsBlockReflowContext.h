/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* class that a parent frame uses to reflow a block frame */

#ifndef nsBlockReflowContext_h_
#define nsBlockReflowContext_h_

#include "mozilla/ReflowOutput.h"

class nsIFrame;
class nsLineBox;
class nsPresContext;
class nsReflowStatus;
namespace mozilla {
class BlockReflowState;
}  // namespace mozilla

/**
 * An encapsulation of the state and algorithm for reflowing block frames.
 */
class nsBlockReflowContext {
  using BlockReflowState = mozilla::BlockReflowState;
  using ReflowInput = mozilla::ReflowInput;
  using ReflowOutput = mozilla::ReflowOutput;

 public:
  nsBlockReflowContext(nsPresContext* aPresContext,
                       const ReflowInput& aParentRI);
  ~nsBlockReflowContext() = default;

  void ReflowBlock(const mozilla::LogicalRect& aSpace, bool aApplyBStartMargin,
                   mozilla::CollapsingMargin& aPrevMargin, nscoord aClearance,
                   nsLineBox* aLine, ReflowInput& aReflowInput,
                   nsReflowStatus& aReflowStatus, BlockReflowState& aState);

  bool PlaceBlock(const ReflowInput& aReflowInput, bool aForceFit,
                  nsLineBox* aLine,
                  mozilla::CollapsingMargin& aBEndMarginResult /* out */,
                  mozilla::OverflowAreas& aOverflowAreas,
                  const nsReflowStatus& aReflowStatus);

  mozilla::CollapsingMargin& GetCarriedOutBEndMargin() {
    return mMetrics.mCarriedOutBEndMargin;
  }

  const ReflowOutput& GetMetrics() const { return mMetrics; }

  /**
   * Computes the collapsed block-start margin (in the context's parent's
   * writing mode) for a block whose reflow input is in aRI.
   * The computed margin is added into aMargin, whose writing mode is the
   * parent's mode as found in mMetrics.GetWritingMode(); note this may not be
   * the block's own writing mode as found in aRI.
   * If aClearanceFrame is null then this is the first optimistic pass which
   * shall assume that no frames have clearance, and we clear the HasClearance
   * on all frames encountered.
   * If non-null, this is the second pass and the caller has decided
   * aClearanceFrame needs clearance (and we will therefore stop collapsing
   * there); also, this function is responsible for marking it with
   * SetHasClearance.
   * If in the optimistic pass any frame is encountered that might possibly
   * need clearance (i.e., if we really needed the optimism assumption) then
   * we set aMayNeedRetry to true.
   * We return true if we changed the clearance state of any line and marked it
   * dirty.
   */
  bool ComputeCollapsedBStartMargin(const ReflowInput& aRI,
                                    mozilla::CollapsingMargin* aMargin,
                                    nsIFrame* aClearanceFrame,
                                    bool* aMayNeedRetry,
                                    bool* aIsEmpty = nullptr);

 protected:
  nsPresContext* mPresContext;
  const ReflowInput& mOuterReflowInput;

  nsIFrame* mFrame;
  mozilla::LogicalRect mSpace;

  nscoord mICoord, mBCoord;
  nsSize mContainerSize;
  mozilla::WritingMode mWritingMode;
  ReflowOutput mMetrics;
  mozilla::CollapsingMargin mBStartMargin;
};

#endif /* nsBlockReflowContext_h_ */
