/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsColumnSetFrame_h_
#define nsColumnSetFrame_h_

/* rendering object for css3 multi-column layout */

#include "nsContainerFrame.h"

class nsCSSBorderRenderer;

/**
 * nsColumnSetFrame implements CSS multi-column layout.
 * @note nsColumnSetFrame keeps true overflow containers in the normal flow
 * child lists (i.e. the principal and overflow lists).
 */
class nsColumnSetFrame final : public nsContainerFrame {
 public:
  NS_DECL_FRAMEARENA_HELPERS(nsColumnSetFrame)

  explicit nsColumnSetFrame(ComputedStyle* aStyle, nsPresContext* aPresContext);

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

#ifdef DEBUG
  void SetInitialChildList(ChildListID aListID,
                           nsFrameList&& aChildList) override;
  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) override;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) override;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) override;
#endif

  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) override;

  nsContainerFrame* GetContentInsertionFrame() override {
    nsIFrame* frame = PrincipalChildList().FirstChild();

    // if no children return nullptr
    if (!frame) {
      return nullptr;
    }

    return frame->GetContentInsertionFrame();
  }

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  /**
   * Similar to nsBlockFrame::DrainOverflowLines. Locate any columns not
   * handled by our prev-in-flow, and any columns sitting on our own
   * overflow list, and put them in our primary child list for reflowing.
   */
  void DrainOverflowColumns();

  // Return the column-content frame.
  void AppendDirectlyOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult) override;

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override {
    return MakeFrameName(u"ColumnSet"_ns, aResult);
  }
#endif

  void CreateBorderRenderers(nsTArray<nsCSSBorderRenderer>& aBorderRenderers,
                             gfxContext* aCtx, const nsRect& aDirtyRect,
                             const nsPoint& aPt);

  Maybe<nscoord> GetNaturalBaselineBOffset(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext aExportContext) const override;

  // Returns true if this column set contains only empty column boxes. For
  // example, this occurs when a multi-column container starts/ends with a
  // column spanner, as the wrapper still constructs an empty column set as
  // the first/last child (see nsCSSFrameConstructor::ConstructBlock).
  bool IsEmpty() override;

 protected:
  nscoord mLastBalanceBSize;
  nsReflowStatus mLastFrameStatus;

  /**
   * These are the parameters that control the layout of columns.
   */
  struct ReflowConfig {
    // The optimal number of columns that we want to use. This is computed from
    // column-count, column-width, available inline-size, etc.
    int32_t mUsedColCount = INT32_MAX;

    // The inline-size of each individual column.
    nscoord mColISize = NS_UNCONSTRAINEDSIZE;

    // The width (inline-size) of each column gap.
    nscoord mColGap = NS_UNCONSTRAINEDSIZE;

    // The available block-size of each individual column. This parameter is set
    // during each iteration of the binary search for the best column
    // block-size.
    nscoord mColBSize = NS_UNCONSTRAINEDSIZE;

    // A boolean controlling whether or not we are balancing.
    bool mIsBalancing = false;

    // A boolean controlling whether or not we are forced to fill columns
    // sequentially.
    bool mForceAuto = false;

    // A boolean indicates whether or not we are in the last attempt to reflow
    // columns. We set it to true at the end of FindBestBalanceBSize().
    bool mIsLastBalancingReflow = false;

    // A boolean indicates whether or not we use an unconstrained available
    // block-size to perform a measuring reflow.
    bool mIsInMeasuringReflow = false;

    // The last known column block-size that was 'feasible'. A column bSize is
    // feasible if all child content fits within the specified bSize.
    nscoord mKnownFeasibleBSize = NS_UNCONSTRAINEDSIZE;

    // The last known block-size that was 'infeasible'. A column bSize is
    // infeasible if not all child content fits within the specified bSize.
    nscoord mKnownInfeasibleBSize = 0;
  };

  // Collect various block-size data calculated in ReflowChildren(), which are
  // mainly used for column balancing. This is the output of ReflowChildren()
  // and ReflowColumns().
  struct ColumnBalanceData {
    // The maximum "content block-size" of any column
    nscoord mMaxBSize = 0;

    // The sum of the "content block-size" for all columns
    nscoord mSumBSize = 0;

    // The "content block-size" of the last column
    nscoord mLastBSize = 0;

    // The maximum "content block-size" of all columns that overflowed
    // their available block-size
    nscoord mMaxOverflowingBSize = 0;

    // The number of columns (starting from 1 because we have at least one
    // column). It can be less than ReflowConfig::mUsedColCount.
    int32_t mColCount = 1;

    // This flag indicates the content that was reflowed fits into the
    // mColMaxBSize in ReflowConfig.
    bool mFeasible = false;
  };

  ColumnBalanceData ReflowColumns(ReflowOutput& aDesiredSize,
                                  const ReflowInput& aReflowInput,
                                  nsReflowStatus& aStatus,
                                  const ReflowConfig& aConfig,
                                  bool aUnboundedLastColumn);

  /**
   * The basic reflow strategy is to call this function repeatedly to
   * obtain specific parameters that determine the layout of the
   * columns. This function will compute those parameters from the CSS
   * style. This function will also be responsible for implementing
   * the state machine that controls column balancing.
   */
  ReflowConfig ChooseColumnStrategy(const ReflowInput& aReflowInput,
                                    bool aForceAuto) const;

  /**
   * Perform the binary search for the best balance block-size for this column
   * set.
   *
   * @param aReflowInput The input parameters for the current reflow iteration.
   * @param aPresContext The presentation context in which the current reflow
   *        iteration is occurring.
   * @param aConfig The ReflowConfig object associated with this column set
   *        frame, generated by ChooseColumnStrategy().
   * @param aColData A data structure used to keep track of data needed between
   *        successive iterations of the balancing process.
   * @param aDesiredSize The final output size of the column set frame (output
   *        of reflow procedure).
   * @param aUnboundedLastColumn A boolean value indicating that the last column
   *        can be of any block-size. Used during the first iteration of the
   *        balancing procedure to measure the block-size of all content in
   *        descendant frames of the column set.
   * @param aStatus A final reflow status of the column set frame, passed in as
   *        an output parameter.
   */
  void FindBestBalanceBSize(const ReflowInput& aReflowInput,
                            nsPresContext* aPresContext, ReflowConfig& aConfig,
                            ColumnBalanceData aColData,
                            ReflowOutput& aDesiredSize,
                            bool aUnboundedLastColumn, nsReflowStatus& aStatus);

  void ForEachColumnRule(
      const std::function<void(const nsRect& lineRect)>& aSetLineRect,
      const nsPoint& aPt) const;

  // MinISize() and PrefISize() are helpers to implement IntrinsicISize().
  nscoord MinISize(const mozilla::IntrinsicSizeInput& aInput);
  nscoord PrefISize(const mozilla::IntrinsicSizeInput& aInput);
};

#endif  // nsColumnSetFrame_h_
