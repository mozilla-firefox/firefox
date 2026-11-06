/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsTableColFrame_h_
#define nsTableColFrame_h_

#include "celldata.h"
#include "mozilla/WritingModes.h"
#include "nsContainerFrame.h"
#include "nsTArray.h"
#include "nsTableColGroupFrame.h"
#include "nscore.h"

namespace mozilla {
class PresShell;
}  // namespace mozilla

class nsTableColFrame final : public nsSplittableFrame {
 public:
  NS_DECL_FRAMEARENA_HELPERS(nsTableColFrame)

  enum {
    eWIDTH_SOURCE_NONE = 0,  // no cell has contributed to the width style
    eWIDTH_SOURCE_CELL = 1,  // a cell specified a width
    eWIDTH_SOURCE_CELL_WITH_SPAN = 2  // a cell implicitly specified a width via
                                      // colspan
  };

  nsTableColType GetColType() const;
  void SetColType(nsTableColType aType);

  /**
   * instantiate a new instance of nsTableRowFrame.
   *
   * @param aPresShell the pres shell for this frame
   *
   * @return           the frame that was created
   */
  friend nsTableColFrame* NS_NewTableColFrame(mozilla::PresShell* aPresShell,
                                              ComputedStyle* aContext);

  // nsIFrame overrides
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override {
    nsSplittableFrame::Init(aContent, aParent, aPrevInFlow);
    if (!aPrevInFlow) {
      mWritingMode = GetTableFrame()->GetWritingMode();
    }
  }

  /** @see nsIFrame::DidSetComputedStyle */
  void DidSetComputedStyle(ComputedStyle* aOldComputedStyle) override;

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override;
#endif

  nsTableColGroupFrame* GetTableColGroupFrame() const {
    nsIFrame* parent = GetParent();
    MOZ_ASSERT(parent && parent->IsTableColGroupFrame());
    return static_cast<nsTableColGroupFrame*>(parent);
  }

  nsTableFrame* GetTableFrame() const {
    return GetTableColGroupFrame()->GetTableFrame();
  }

  int32_t GetColIndex() const;

  void SetColIndex(int32_t aColIndex);

  nsTableColFrame* GetNextCol() const;

  /** return the number of the columns the col represents.  always >= 1 */
  int32_t GetSpan();

  nscoord GetIStartBorderWidth() const { return mIStartBorderWidth; }
  nscoord GetIEndBorderWidth() const { return mIEndBorderWidth; }
  void SetIStartBorderWidth(nscoord aWidth) { mIStartBorderWidth = aWidth; }
  void SetIEndBorderWidth(nscoord aWidth) { mIEndBorderWidth = aWidth; }

#ifdef DEBUG
  void Dump(int32_t aIndent);
#endif

  /**
   * Restore the default values of the intrinsic widths, so that we can
   * re-accumulate intrinsic widths from the cells in the column.
   */
  void ResetIntrinsics() {
    mMinCoord = 0;
    mPrefCoord = 0;
    mPrefPercent = 0.0f;
    mHasSpecifiedCoord = false;
  }

  /**
   * Restore the default value of the preferred percentage width (the
   * only intrinsic width used by FixedTableLayoutStrategy.
   */
  void ResetPrefPercent() { mPrefPercent = 0.0f; }

  /**
   * Restore the default values of the temporary buffer for
   * spanning-cell intrinsic widths (as we process spanning cells).
   */
  void ResetSpanIntrinsics() {
    mSpanMinCoord = 0;
    mSpanPrefCoord = 0;
    mSpanPrefPercent = 0.0f;
  }

  /**
   * Add the widths for a cell or column element, or the contribution of
   * the widths from a column-spanning cell:
   * @param aMinCoord The minimum intrinsic width
   * @param aPrefCoord The preferred intrinsic width or, if there is a
   *   specified non-percentage width, max(specified width, minimum intrinsic
   *   width).
   * @param aHasSpecifiedCoord Whether there is a specified
   *   non-percentage width.
   *
   * Note that the implementation of this functions is a bit tricky
   * since mPrefCoord means different things depending on
   * whether mHasSpecifiedCoord is true (and likewise for aPrefCoord and
   * aHasSpecifiedCoord).  If mHasSpecifiedCoord is false, then
   * all widths added had aHasSpecifiedCoord false and mPrefCoord is the
   * largest of the pref widths.  But if mHasSpecifiedCoord is true,
   * then mPrefCoord is the largest of (1) the pref widths for cells
   * with aHasSpecifiedCoord true and (2) the min widths for cells with
   * aHasSpecifiedCoord false.
   */
  void AddCoords(nscoord aMinCoord, nscoord aPrefCoord,
                 bool aHasSpecifiedCoord) {
    NS_ASSERTION(aMinCoord <= aPrefCoord, "intrinsic widths out of order");

    if (aHasSpecifiedCoord && !mHasSpecifiedCoord) {
      mPrefCoord = mMinCoord;
      mHasSpecifiedCoord = true;
    }
    if (!aHasSpecifiedCoord && mHasSpecifiedCoord) {
      aPrefCoord = aMinCoord;  // NOTE: modifying argument
    }

    if (aMinCoord > mMinCoord) {
      mMinCoord = aMinCoord;
    }
    if (aPrefCoord > mPrefCoord) {
      mPrefCoord = aPrefCoord;
    }

    NS_ASSERTION(mMinCoord <= mPrefCoord, "min larger than pref");
  }

  /**
   * Add a percentage width specified on a cell or column element or the
   * contribution to this column of a percentage width specified on a
   * column-spanning cell.
   */
  void AddPrefPercent(float aPrefPercent) {
    if (aPrefPercent > mPrefPercent) {
      mPrefPercent = aPrefPercent;
    }
  }

  /**
   * Get the largest minimum intrinsic width for this column.
   */
  nscoord GetMinCoord() const { return mMinCoord; }
  /**
   * Get the largest preferred width for this column, or, if there were
   * any specified non-percentage widths (see GetHasSpecifiedCoord), the
   * largest minimum intrinsic width or specified width.
   */
  nscoord GetPrefCoord() const { return mPrefCoord; }
  /**
   * Get whether there were any specified widths contributing to this
   * column.
   */
  bool GetHasSpecifiedCoord() const { return mHasSpecifiedCoord; }

  /**
   * Get the largest specified percentage width contributing to this
   * column (returns 0 if there were none).
   */
  float GetPrefPercent() const { return mPrefPercent; }

  /**
   * Like AddCoords, but into a temporary buffer used for groups of
   * column-spanning cells.
   */
  void AddSpanCoords(nscoord aSpanMinCoord, nscoord aSpanPrefCoord,
                     bool aSpanHasSpecifiedCoord) {
    NS_ASSERTION(aSpanMinCoord <= aSpanPrefCoord,
                 "intrinsic widths out of order");

    if (!aSpanHasSpecifiedCoord && mHasSpecifiedCoord) {
      aSpanPrefCoord = aSpanMinCoord;  // NOTE: modifying argument
    }

    if (aSpanMinCoord > mSpanMinCoord) {
      mSpanMinCoord = aSpanMinCoord;
    }
    if (aSpanPrefCoord > mSpanPrefCoord) {
      mSpanPrefCoord = aSpanPrefCoord;
    }

    NS_ASSERTION(mSpanMinCoord <= mSpanPrefCoord, "min larger than pref");
  }

  /*
   * Accumulate percentage widths on column spanning cells into
   * temporary variables.
   */
  void AddSpanPrefPercent(float aSpanPrefPercent) {
    if (aSpanPrefPercent > mSpanPrefPercent) {
      mSpanPrefPercent = aSpanPrefPercent;
    }
  }

  /*
   * Accumulate the temporary variables for column spanning cells into
   * the primary variables.
   */
  void AccumulateSpanIntrinsics() {
    AddCoords(mSpanMinCoord, mSpanPrefCoord, mHasSpecifiedCoord);
    AddPrefPercent(mSpanPrefPercent);
  }

  // Used to adjust a column's pref percent so that the table's total
  // never exceeeds 100% (by only allowing percentages to be used,
  // starting at the first column, until they reach 100%).
  void AdjustPrefPercent(float* aTableTotalPercent) {
    float allowed = 1.0f - *aTableTotalPercent;
    if (mPrefPercent > allowed) {
      mPrefPercent = allowed;
    }
    *aTableTotalPercent += mPrefPercent;
  }

  // The final width of the column.
  void ResetFinalISize() {
    mFinalISize = nscoord_MIN;  // so we detect that it changed
  }
  void SetFinalISize(nscoord aFinalISize) { mFinalISize = aFinalISize; }
  nscoord GetFinalISize() { return mFinalISize; }

  void InvalidateFrame(uint32_t aDisplayItemKey = 0,
                       bool aRebuildDisplayItems = true) override;
  void InvalidateFrameWithRect(const nsRect& aRect,
                               uint32_t aDisplayItemKey = 0,
                               bool aRebuildDisplayItems = true) override;
  void InvalidateFrameForRemoval() override { InvalidateFrameSubtree(); }

 protected:
  explicit nsTableColFrame(ComputedStyle* aStyle, nsPresContext* aPresContext);
  ~nsTableColFrame();

  nscoord mMinCoord;
  nscoord mPrefCoord;
  nscoord mSpanMinCoord;   // XXX...
  nscoord mSpanPrefCoord;  // XXX...
  float mPrefPercent;
  float mSpanPrefPercent;  // XXX...
  // ...XXX the four members marked above could be allocated as part of
  // a separate array allocated only during
  // BasicTableLayoutStrategy::ComputeColumnIntrinsicISizes (and only
  // when colspans were present).
  nscoord mFinalISize;

  // the index of the column with respect to the whole table (starting at 0)
  // it should never be smaller then the start column index of the parent
  // colgroup
  uint32_t mColIndex;

  // border widths of the inner half of the border only
  nscoord mIStartBorderWidth;
  nscoord mIEndBorderWidth;

  bool mHasSpecifiedCoord;
};

inline int32_t nsTableColFrame::GetColIndex() const { return mColIndex; }

inline void nsTableColFrame::SetColIndex(int32_t aColIndex) {
  mColIndex = aColIndex;
}

#endif
