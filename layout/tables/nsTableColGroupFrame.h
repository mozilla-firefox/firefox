/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef nsTableColGroupFrame_h_
#define nsTableColGroupFrame_h_

#include "mozilla/WritingModes.h"
#include "nsContainerFrame.h"
#include "nsTableFrame.h"
#include "nscore.h"

class nsTableColFrame;

namespace mozilla {
class PresShell;
}  // namespace mozilla

/**
 * nsTableColGroupFrame
 * data structure to maintain information about a single table cell's frame
 */
class nsTableColGroupFrame final : public nsContainerFrame {
 public:
  NS_DECL_FRAMEARENA_HELPERS(nsTableColGroupFrame)
  NS_DECL_QUERYFRAME

  /**
   * instantiate a new instance of nsTableRowFrame.
   *
   * @param aPresShell the pres shell for this frame
   *
   * @return           the frame that was created
   */
  friend nsTableColGroupFrame* NS_NewTableColGroupFrame(
      mozilla::PresShell* aPresShell, ComputedStyle* aStyle);

  // nsIFrame overrides
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override {
    nsContainerFrame::Init(aContent, aParent, aPrevInFlow);
    if (!aPrevInFlow) {
      mWritingMode = GetTableFrame()->GetWritingMode();
    }
  }

  nsTableFrame* GetTableFrame() const {
    nsIFrame* parent = GetParent();
    MOZ_ASSERT(parent && parent->IsTableFrame());
    MOZ_ASSERT(!parent->GetPrevInFlow(),
               "Col group should always be in a first-in-flow table frame");
    return static_cast<nsTableFrame*>(parent);
  }

  // Gets the synthetic colgroup of our table.
  nsTableColGroupFrame* GetSyntheticColGroup() const;

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  /** A colgroup can be caused by three things:
   * 1) An element with table-column-group display
   * 2) An element with a table-column display without a
   *    table-column-group parent
   * 3) Cells that are not in a column (and hence get an anonymous
   *    column and colgroup).
   *
   * In practice, we don't need to differentiate between cases (1) and (2),
   * because they both correspond to table-column-group boxes in the spec and
   * hence have observably identical behavior.  Case three is flagged as a
   * synthetic colgroup, because it may need to have different behavior in some
   * cases.
   */
  bool IsSynthetic() const;
  void SetIsSynthetic();

  /** @see nsIFrame::DidSetComputedStyle */
  void DidSetComputedStyle(ComputedStyle* aOldComputedStyle) override;

  void SetInitialChildList(ChildListID aListID,
                           nsFrameList&& aChildList) override;
  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) override;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) override;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) override;

  /** remove the column aChild from the column group, if requested renumber
   * the subsequent columns in this column group and all following column
   * groups. see also ResetColIndices for this
   * @param aChild       - the column frame that needs to be removed
   * @param aResetSubsequentColIndices - if true the columns that follow
   *                                     after aChild will be reenumerated
   */
  void RemoveChild(DestroyContext& aContext, nsTableColFrame& aChild,
                   bool aResetSubsequentColIndices);

  /** reflow of a column group is a trivial matter of reflowing
   * the col group's children (columns), and setting this frame
   * to 0-size.  Since tables are row-centric, column group frames
   * don't play directly in the rendering game.  They do however
   * maintain important state that effects table and cell layout.
   */
  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  /** Add column frames to the table storages: colframe cache and cellmap
   * this doesn't change the mFrames of the colgroup frame.
   * @param aFirstColIndex - the index at which aFirstFrame should be inserted
   *                         into the colframe cache.
   * @param aResetSubsequentColIndices - the indices of the col frames
   *                                     after the insertion might need
   *                                     an update
   * @param aCols - an iterator that can be used to iterate over the col
   *                frames to be added.  Once this is done, the frames on the
   *                sbling chain of its .get() at that point will still need
   *                their col indices updated.
   * @result            - if there is no table frame or the table frame is not
   *                      the first in flow it will return an error
   */
  nsresult AddColsToTable(int32_t aFirstColIndex,
                          bool aResetSubsequentColIndices,
                          const nsFrameList::Slice& aCols);

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override;
  void Dump(int32_t aIndent);
#endif

  /** returns the number of columns represented by this group.
   * if there are col children, count them (taking into account the span of
   * each) else, check my own span attribute.
   */
  int32_t GetColCount() const;

  /** first column on the child list */
  nsTableColFrame* GetFirstColumn();
  /** next sibling to aChildFrame that is a column frame, first column frame
   * in the column group if aChildFrame is null
   */
  nsTableColFrame* GetNextColumn(nsIFrame* aChildFrame);

  /** @return - the position of the first column in this colgroup in the table
   * colframe cache.
   */
  int32_t GetStartColumnIndex();

  /** set the position of the first column in this colgroup in the table
   * colframe cache.
   */
  void SetStartColumnIndex(int32_t aIndex);

  /** helper method to get the span attribute for this colgroup */
  int32_t GetSpan();

  /** provide access to the mFrames list
   */
  nsFrameList& GetWritableChildList();

  /** set the column index for all frames starting at aStartColFrame, it
   * will also reset the column indices in all subsequent colgroups
   * @param aFirstFrame - start the reset operation from this frame
   * @param aSyntheticColGroup - the explicit reference to the synthetic col
   *                             group which is always logically at the end.
   * @param aFirstColIndex - first column that is reset should get this index
   * @param aStartColFrame - if specified the reset starts with this column
   *                         inside the colgroup; if not specified, the reset
   *                         starts with the first column
   */
  static void ResetColIndices(nsIFrame* aFirstFrame,
                              nsTableColGroupFrame* aSyntheticColGroup,
                              int32_t aFirstColIndex,
                              nsIFrame* aStartColFrame = nullptr);

  void InvalidateFrame(uint32_t aDisplayItemKey = 0,
                       bool aRebuildDisplayItems = true) override;
  void InvalidateFrameWithRect(const nsRect& aRect,
                               uint32_t aDisplayItemKey = 0,
                               bool aRebuildDisplayItems = true) override;
  void InvalidateFrameForRemoval() override { InvalidateFrameSubtree(); }

 protected:
  nsTableColGroupFrame(ComputedStyle* aStyle, nsPresContext* aPresContext);

  void InsertColsReflow(int32_t aColIndex, const nsFrameList::Slice& aCols);

  LogicalSides GetLogicalSkipSides() const override;

  // data members
  int32_t mColCount;
  // the starting column index this col group represents. Must be >= 0.
  int32_t mStartColIndex;
};

inline nsTableColGroupFrame::nsTableColGroupFrame(ComputedStyle* aStyle,
                                                  nsPresContext* aPresContext)
    : nsContainerFrame(aStyle, aPresContext, kClassID),
      mColCount(0),
      mStartColIndex(0) {}

inline int32_t nsTableColGroupFrame::GetStartColumnIndex() {
  return mStartColIndex;
}

inline void nsTableColGroupFrame::SetStartColumnIndex(int32_t aIndex) {
  mStartColIndex = aIndex;
}

inline int32_t nsTableColGroupFrame::GetColCount() const { return mColCount; }

inline nsFrameList& nsTableColGroupFrame::GetWritableChildList() {
  return mFrames;
}

#endif
