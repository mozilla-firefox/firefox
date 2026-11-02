/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * rendering object for CSS display:block, inline-block, and list-item
 * boxes, also used for various anonymous boxes
 */

#ifndef nsBlockFrame_h_
#define nsBlockFrame_h_

#include "mozilla/IntrinsicISizesCache.h"
#include "nsContainerFrame.h"
#include "nsFloatManager.h"
#include "nsHTMLParts.h"
#include "nsLineBox.h"

enum class LineReflowStatus {
  // The line was completely reflowed and fit in available width, and we should
  // try to pull up content from the next line if possible.
  OK,
  // The line was completely reflowed and fit in available width, but we should
  // not try to pull up content from the next line.
  Stop,
  // We need to reflow the line again at its current vertical position. The
  // new reflow should not try to pull up any frames from the next line.
  RedoNoPull,
  // We need to reflow the line again using the floats from its height
  // this reflow, since its height made it hit floats that were not
  // adjacent to its top.
  RedoMoreFloats,
  // We need to reflow the line again at a lower vertical postion where there
  // may be more horizontal space due to different float configuration.
  RedoNextBand,
  // The line did not fit in the available vertical space. Try pushing it to
  // the next page or column if it's not the first line on the current
  // page/column.
  Truncated
};

class nsBlockInFlowLineIterator;
class nsLineLayout;
namespace mozilla {
class BlockReflowState;
class PresShell;
class ServoRestyleState;
class ServoStyleSet;

}  // namespace mozilla

/**
 * Some invariants:
 * -- The overflow floats list contains the float frames
 * whose placeholders are in the overflow list.
 * -- A given piece of content has at most one placeholder
 * frame in a block's normal child list.
 * -- While a block is being reflowed, and from then until
 * its next-in-flow is reflowed it may have a
 * PushedFloatProperty frame property that points to
 * an nsFrameList. This list contains continuations for
 * floats whose prev-in-flow is in the block's regular float
 * list and first-in-flows of floats that did not fit, but
 * whose placeholders are in the block or one of its
 * prev-in-flows.
 * -- In all these frame lists, if there are two frames for
 * the same content appearing in the list, then the frames
 * appear with the prev-in-flow before the next-in-flow.
 * -- While reflowing a block, its overflow line list
 * will usually be empty but in some cases will have lines
 * (while we reflow the block at its shrink-wrap width).
 * In this case any new overflowing content must be
 * prepended to the overflow lines.
 */

/*
 * Base class for block and inline frames.
 * The block frame has an additional child list, FrameChildListID::Absolute,
 * which contains the absolutely positioned frames.
 */
class nsBlockFrame : public nsContainerFrame {
  using BlockReflowState = mozilla::BlockReflowState;

 public:
  NS_DECL_FRAMEARENA_HELPERS(nsBlockFrame)

  typedef nsLineList::iterator LineIterator;
  typedef nsLineList::const_iterator ConstLineIterator;
  typedef nsLineList::reverse_iterator ReverseLineIterator;
  typedef nsLineList::const_reverse_iterator ConstReverseLineIterator;

  LineIterator LinesBegin() { return mLines.begin(); }
  LineIterator LinesEnd() { return mLines.end(); }
  ConstLineIterator LinesBegin() const { return mLines.begin(); }
  ConstLineIterator LinesEnd() const { return mLines.end(); }
  ReverseLineIterator LinesRBegin() { return mLines.rbegin(); }
  ReverseLineIterator LinesREnd() { return mLines.rend(); }
  ConstReverseLineIterator LinesRBegin() const { return mLines.rbegin(); }
  ConstReverseLineIterator LinesREnd() const { return mLines.rend(); }
  LineIterator LinesBeginFrom(nsLineBox* aList) { return mLines.begin(aList); }
  ReverseLineIterator LinesRBeginFrom(nsLineBox* aList) {
    return mLines.rbegin(aList);
  }

  // Methods declared to be used in 'range-based-for-loop'
  nsLineList& Lines() { return mLines; }
  const nsLineList& Lines() const { return mLines; }

  friend nsBlockFrame* NS_NewBlockFrame(mozilla::PresShell* aPresShell,
                                        ComputedStyle* aStyle);

  // nsQueryFrame
  NS_DECL_QUERYFRAME

  // nsIFrame
  void Init(nsIContent* aContent, nsContainerFrame* aParent,
            nsIFrame* aPrevInFlow) override;
  void SetInitialChildList(ChildListID aListID,
                           nsFrameList&& aChildList) override;
  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) override;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) override;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame* aOldFrame) override;
  nsContainerFrame* GetContentInsertionFrame() override;
  void AppendDirectlyOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult) override;
  const nsFrameList& GetChildList(ChildListID aListID) const override;
  void GetChildLists(nsTArray<ChildList>* aLists) const override;
  nscoord SynthesizeFallbackBaseline(
      mozilla::WritingMode aWM,
      BaselineSharingGroup aBaselineGroup) const override;
  BaselineSharingGroup GetDefaultBaselineSharingGroup() const override {
    return BaselineSharingGroup::Last;
  }
  Maybe<nscoord> GetNaturalBaselineBOffset(
      mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext aExportContext) const override;
  nscoord GetCaretBaseline() const override;
  void Destroy(DestroyContext&) override;

  bool IsFloatContainingBlock() const override;
  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  void InvalidateFrame(uint32_t aDisplayItemKey = 0,
                       bool aRebuildDisplayItems = true) override;
  void InvalidateFrameWithRect(const nsRect& aRect,
                               uint32_t aDisplayItemKey = 0,
                               bool aRebuildDisplayItems = true) override;

#ifdef DEBUG_FRAME_DUMP
  void List(FILE* out = stderr, const char* aPrefix = "",
            ListFlags aFlags = ListFlags()) const override;
  nsresult GetFrameName(nsAString& aResult) const override;
#endif

#ifdef ACCESSIBILITY
  mozilla::a11y::AccType AccessibleType() override;
#endif

  // Line cursor methods to speed up line searching in which one query result
  // is expected to be close to the next in general. This is mainly for
  // searching line(s) containing a point. It is also used as a cache for local
  // computation. The basic idea for the former is that we set the cursor
  // property if the lines' overflowArea.InkOverflow().ys and
  // overflowArea.InkOverflow().yMosts are non-decreasing
  // (considering only non-empty overflowArea.InkOverflow()s; empty
  // overflowArea.InkOverflow()s never participate in event handling
  // or painting), and the block has sufficient number of lines. The
  // cursor property points to a "recently used" line. If we get a
  // series of requests that work on lines
  // "near" the cursor, then we can find those nearby lines quickly by
  // starting our search at the cursor.

  // We have two independent line cursors, one used for display-list building
  // and the other for a11y or other frame queries. Either or both may be
  // present at any given time. When we reflow or otherwise munge the lines,
  // both cursors will be cleared.
  // The display cursor is only created and used if the lines satisfy the non-
  // decreasing y-coordinate condition (see SetupLineCursorForDisplay comment
  // below), whereas the query cursor may be created for any block. The two
  // are separated so creating a cursor for a11y queries (eg GetRenderedText)
  // does not risk confusing the display-list building code.

  // Clear out line cursors because we're disturbing the lines (i.e., Reflow)
  void ClearLineCursors() {
    if (MaybeHasLineCursor()) {
      ClearLineCursorForDisplay();
      ClearLineCursorForQuery();
      RemoveStateBits(NS_BLOCK_HAS_LINE_CURSOR);
    }
    ClearLineIterator();
  }
  void ClearLineCursorForDisplay() {
    RemoveProperty(LineCursorPropertyDisplay());
  }
  void ClearLineCursorForQuery() { RemoveProperty(LineCursorPropertyQuery()); }

  // Clear just the line-iterator property; this is used if we need to get a
  // LineIterator temporarily during reflow, when using a persisted iterator
  // would be invalid. So we clear the stored property immediately after use.
  void ClearLineIterator() { RemoveProperty(LineIteratorProperty()); }

  // Get the first line that might contain y-coord 'y', or nullptr if you must
  // search all lines. If nonnull is returned then we guarantee that the lines'
  // combinedArea.ys and combinedArea.yMosts are non-decreasing.
  // The actual line returned might not contain 'y', but if not, it is
  // guaranteed to be before any line which does contain 'y'.
  nsLineBox* GetFirstLineContaining(nscoord y);

  // Ensure the frame has a display-list line cursor, initializing it to the
  // first line if it is not already present. (If there's an existing cursor,
  // it is left untouched.) Only call this if you guarantee that the lines'
  // combinedArea.ys and combinedArea.yMosts are non-decreasing.
  void SetupLineCursorForDisplay();

  // Ensure the frame has a query line cursor, initializing it to the first
  // line if it is not already present. (If there's an existing cursor, it is
  // left untouched.)
  void SetupLineCursorForQuery();

  void ChildIsDirty(nsIFrame* aChild) override;

  bool IsEmpty() override;
  bool CachedIsEmpty() override;
  bool IsSelfEmpty() override;
  bool LinesAreEmpty() const;

  // Given that we have a ::marker frame, does it actually draw something, i.e.,
  // do we have either a 'list-style-type' or 'list-style-image' that is
  // not 'none', and no 'content'?
  // This is expected to be used only for outside markers, and when the caller
  // already has a pointer to the marker frame.
  bool MarkerIsEmpty(const nsIFrame* aMarker) const;

  // Return true if this frame has a ::marker frame.
  bool HasMarker() const { return HasAnyStateBits(NS_BLOCK_HAS_MARKER); }

  // Return true if this frame has an outside ::marker frame.
  bool HasOutsideMarker() const;

  /**
   * @return the first-letter frame or nullptr if we don't have one.
   */
  nsIFrame* GetFirstLetter() const;

  /**
   * @return the ::first-line frame or nullptr if we don't have one.
   */
  nsIFrame* GetFirstLineFrame() const;

  void MarkIntrinsicISizesDirty() override;

 private:
  // Whether CSS text-indent should be applied to the given line.
  bool TextIndentAppliesTo(const LineIterator& aLine) const;

  void CheckIntrinsicCacheAgainstShrinkWrapState();

  nsRect ComputePaddingInflatedScrollableOverflow(
      const nsRect& aInFlowChildBounds) const;
  Maybe<nsRect> GetLineFrameInFlowBounds(
      const nsLineBox& aLine, const nsIFrame& aLineChildFrame,
      bool aConsiderPositiveMargins = true) const;

  template <typename LineIteratorType>
  Maybe<nscoord> GetBaselineBOffset(LineIteratorType aStart,
                                    LineIteratorType aEnd,
                                    mozilla::WritingMode aWM,
                                    BaselineSharingGroup aBaselineGroup,
                                    BaselineExportContext aExportContext) const;

 protected:
  // MinISize() and PrefISize() are helpers to implement IntrinsicISize().
  nscoord MinISize(const mozilla::IntrinsicSizeInput& aInput);
  nscoord PrefISize(const mozilla::IntrinsicSizeInput& aInput);

 public:
  nscoord IntrinsicISize(const mozilla::IntrinsicSizeInput& aInput,
                         mozilla::IntrinsicISizeType aType) override;

  nsRect ComputeTightBounds(DrawTarget* aDrawTarget) const override;

  nsresult GetPrefWidthTightBounds(gfxContext* aContext, nscoord* aX,
                                   nscoord* aXMost) override;

  /**
   * Compute the final block size of this frame.
   *
   * @param aState BlockReflowState passed from parent during reflow.
   *        Note: aState.mReflowStatus is mostly an "input" parameter. When this
   *        method is called, it should represent what our status would be as if
   *        we were shrinkwrapping our children's block-size. This method will
   *        then adjust it before returning if our status is different in light
   *        of our actual final block-size and current page/column's available
   *        block-size.
   * @param aBEndEdgeOfChildren The distance between this frame's block-start
   *        border-edge and the block-end edge of our last child's border-box.
   *        This is effectively our block-start border-padding plus the
   *        block-size of our children, precomputed outside of this function.
   * @return our final block-size with respect to aReflowInput's writing-mode.
   */
  nscoord ComputeFinalBSize(BlockReflowState& aState,
                            nscoord aBEndEdgeOfChildren);

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;

  /**
   * Move any frames on our overflow list to the end of our principal list.
   * @return true if there were any overflow frames
   */
  bool DrainSelfOverflowList() override;

  void StealFrame(nsIFrame* aChild) override;

  void DeleteNextInFlowChild(DestroyContext&, nsIFrame* aNextInFlow,
                             bool aDeletingEmptyFrames) override;

  /**
   * This is a special method that allows a child class of nsBlockFrame to
   * return a special, customized nsStyleText object to the nsLineLayout
   * constructor. It is used when the nsBlockFrame child needs to specify its
   * custom rendering style.
   */
  virtual const nsStyleText* StyleTextForLineLayout();

  /**
   * Determines whether the collapsed margin carried out of the last
   * line includes the margin-top of a line with clearance (in which
   * case we must avoid collapsing that margin with our bottom margin)
   */
  bool CheckForCollapsedBEndMarginFromClearanceLine();

  static nsresult GetCurrentLine(BlockReflowState* aState,
                                 nsLineBox** aOutCurrentLine);

  /**
   * Determine if this block is a margin root at the top/bottom edges.
   */
  void IsMarginRoot(bool* aBStartMarginRoot, bool* aBEndMarginRoot);

  static bool BlockNeedsFloatManager(nsIFrame* aBlock);

  /**
   * Returns whether aFrame is a block frame that will wrap its contents
   * around floats intruding on it from the outside.  (aFrame need not
   * be a block frame, but if it's not, the result will be false.)
   *
   * Note: We often use the term "float-avoiding block" to refer to
   * block-level frames for whom this function returns false.
   */
  static bool BlockCanIntersectFloats(nsIFrame* aFrame);

  /**
   * Returns the inline size that needs to be cleared past floats for
   * blocks that avoid (i.e. cannot intersect) floats.  aState must already
   * have GetFloatAvailableSpace called on it for the block-dir position that
   * we care about (which need not be its current mBCoord)
   */
  struct FloatAvoidingISizeToClear {
    // Note that we care about the inline-start margin but can ignore
    // the inline-end margin.
    nscoord marginIStart, borderBoxISize;
  };
  static FloatAvoidingISizeToClear ISizeToClearPastFloats(
      const BlockReflowState& aState,
      const mozilla::LogicalRect& aFloatAvailableSpace,
      nsIFrame* aFloatAvoidingBlock);

  /**
   * Creates a contination for aFloat and adds it to the list of overflow
   * floats. Also updates aState.mReflowStatus to include the float's
   * incompleteness. Must only be called while this block frame is in reflow.
   * aFloatStatus must be the float's true, unmodified reflow status.
   */
  void SplitFloat(BlockReflowState& aState, nsIFrame* aFloat,
                  const nsReflowStatus& aFloatStatus);

  /**
   * Walks up the frame tree, starting with aCandidate, and returns the first
   * block frame that it encounters.
   */
  static nsBlockFrame* GetNearestAncestorBlock(nsIFrame* aCandidate);

  struct FrameLines {
    nsLineList mLines;
    nsFrameList mFrames;
  };

  /**
   * Update the styles of our various pseudo-elements (marker, first-line,
   * etc, but _not_ first-letter).
   */
  void UpdatePseudoElementStyles(mozilla::ServoRestyleState& aRestyleState);

  // Update our first-letter styles during stylo post-traversal.  This needs to
  // be done at a slightly different time than our other pseudo-elements.
  void UpdateFirstLetterStyle(mozilla::ServoRestyleState& aRestyleState);

 protected:
  explicit nsBlockFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                        ClassID aID = kClassID)
      : nsContainerFrame(aStyle, aPresContext, aID) {
#ifdef DEBUG
    InitDebugFlags();
#endif
  }

  virtual ~nsBlockFrame();

  void DidSetComputedStyle(ComputedStyle* aOldStyle) override;

#ifdef DEBUG
  already_AddRefed<ComputedStyle> GetFirstLetterStyle(
      nsPresContext* aPresContext);
#endif

  NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(LineCursorPropertyDisplay, nsLineBox)
  NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(LineCursorPropertyQuery, nsLineBox)
  // Note that the NS_BLOCK_HAS_LINE_CURSOR flag does not necessarily mean the
  // cursor is present, as it covers both the "display" and "query" cursors,
  // but may remain set if they have been separately deleted. In such a case,
  // the Get* accessors will be slightly more expensive, but will still safely
  // return null if the cursor is absent.
  bool MaybeHasLineCursor() {
    return HasAnyStateBits(NS_BLOCK_HAS_LINE_CURSOR);
  }
  nsLineBox* GetLineCursorForDisplay() {
    return MaybeHasLineCursor() ? GetProperty(LineCursorPropertyDisplay())
                                : nullptr;
  }
  nsLineBox* GetLineCursorForQuery() {
    return MaybeHasLineCursor() ? GetProperty(LineCursorPropertyQuery())
                                : nullptr;
  }

  void SetLineCursorForDisplay(nsLineBox* aLine) {
    MOZ_ASSERT(aLine, "must have a line");
    MOZ_ASSERT(!mLines.empty(), "aLine isn't my line");
    SetProperty(LineCursorPropertyDisplay(), aLine);
    AddStateBits(NS_BLOCK_HAS_LINE_CURSOR);
  }

  nsLineBox* NewLineBox(nsIFrame* aFrame, bool aIsBlock) {
    return NS_NewLineBox(aFrame, aIsBlock);
  }
  nsLineBox* NewLineBox(nsLineBox* aFromLine, nsIFrame* aFrame,
                        int32_t aCount) {
    return NS_NewLineBox(aFromLine, aFrame, aCount);
  }
  void FreeLineBox(nsLineBox* aLine) {
    if (aLine == GetLineCursorForDisplay()) {
      ClearLineCursorForDisplay();
    }
    if (aLine == GetLineCursorForQuery()) {
      ClearLineCursorForQuery();
    }
    aLine->Destroy();
  }
  /**
   * Helper method for StealFrame.
   */
  void RemoveFrameFromLine(nsIFrame* aChild, nsLineList::iterator aLine,
                           nsFrameList& aFrameList, nsLineList& aLineList);

  void TryAllLines(nsLineList::iterator* aIterator,
                   nsLineList::iterator* aStartIterator,
                   nsLineList::iterator* aEndIterator, bool* aInOverflowLines,
                   FrameLines** aOverflowLines);

  /** move the frames contained by aLine by aDeltaBCoord
   * if aLine is a block, its child floats are added to the state manager
   */
  void SlideLine(BlockReflowState& aState, nsLineBox* aLine,
                 nscoord aDeltaBCoord);

  void UpdateLineContainerSize(nsLineBox* aLine,
                               const nsSize& aNewContainerSize);

  // helper for SlideLine and UpdateLineContainerSize
  void MoveChildFramesOfLine(nsLineBox* aLine, nscoord aDeltaBCoord);

  // Returns block-end edge of children.
  nscoord ComputeFinalSize(const ReflowInput& aReflowInput,
                           BlockReflowState& aState, ReflowOutput& aMetrics);

  /**
   * Calculates the necessary shift to honor 'align-content' and applies it.
   */
  void AlignContent(BlockReflowState& aState, ReflowOutput& aMetrics,
                    nscoord aBEndEdgeOfChildren);
  // Stash the effective align-content shift value between reflows
  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(AlignContentShift, nscoord)

  /**
   * Helper method for Reflow(). Computes the overflow areas created by our
   * children, and includes them into aOverflowAreas.
   */
  void ComputeOverflowAreas(mozilla::OverflowAreas& aOverflowAreas,
                            const nsStyleDisplay* aDisplay) const;

  /**
   * Add the frames in aFrameList to this block after aPrevSibling.
   * This block thinks in terms of lines, but the frame construction code
   * knows nothing about lines at all so we need to find the line that
   * contains aPrevSibling and add aFrameList after aPrevSibling on that line.
   * New lines are created as necessary to handle block data in aFrameList.
   * This function will clear aFrameList.
   *
   * aPrevSiblingLine, if present, must be the line containing aPrevSibling.
   * Providing it will make this function faster.
   */
  void AddFrames(nsFrameList&& aFrameList, nsIFrame* aPrevSibling,
                 const nsLineList::iterator* aPrevSiblingLine);

  // Return the :-moz-block-ruby-content child frame, if any.
  // (It's non-null only if this block frame is for 'display:block ruby'.)
  nsContainerFrame* GetRubyContentPseudoFrame();

  /**
   * Perform Bidi resolution on this frame
   */
  nsresult ResolveBidi();

 public:
  bool IsButtonControlFrame() const {
    return IsInputButtonControlFrame() || IsColorControlFrame() ||
           IsComboboxControlFrame();
  }

  bool IsTextInput() const {
    return Style()->GetPseudoType() ==
               mozilla::PseudoStyleType::MozScrolledContent &&
           mParent->IsTextInputFrame();
  }

  bool IsSingleLineTextInput() const {
    return IsTextInput() && mContent->IsHTMLElement(nsGkAtoms::input);
  }

  bool IsButtonLike() const {
    if (mContent->IsAnyOfHTMLElements(nsGkAtoms::button)) {
      // NOTE(emilio): We need the IsAnonBox check to deal with things like the
      // :-moz-anonymous-item of a <button> with display: grid. We don't want
      // that to e.g. center its contents. But we do want the scrolled-content
      // box of a button to do it.
      auto pseudoType = Style()->GetPseudoType();
      return !mozilla::PseudoStyle::IsAnonBox(pseudoType) ||
             pseudoType == mozilla::PseudoStyleType::MozScrolledContent;
    }
    return IsButtonControlFrame();
  }

  bool IsButtonOrTextInput() const { return IsButtonLike() || IsTextInput(); }

  /** Returns the effective align-content of this frame */
  mozilla::StyleAlignFlags EffectiveAlignContent() const {
    auto alignContent = StylePosition()->mAlignContent.primary;
    if (alignContent == mozilla::StyleAlignFlags::NORMAL ||
        !mozilla::StaticPrefs::
            layout_forms_button_input_align_content_block_enabled()) {
      if (IsButtonLike()) {
        return mozilla::StyleAlignFlags::CENTER;
      }
      if (IsSingleLineTextInput()) {
        return mozilla::StyleAlignFlags::CENTER |
               mozilla::StyleAlignFlags::UNSAFE;
      }
    }
    return alignContent;
  }

  bool IsContentAligned() const {
    return EffectiveAlignContent() != mozilla::StyleAlignFlags::NORMAL;
  }

 protected:
  nscoord GetAlignContentShift() const {
    return IsContentAligned() ? GetProperty(AlignContentShift()) : 0;
  }

  /**
   * For text-wrap:balance, we iteratively try reflowing with adjusted inline
   * size to find the "best" result (the tightest size that can be applied
   * without increasing the total line count of the block).
   *
   * For text-box-trim, if requested by the previous trial of the frame,
   * we perform a single retry in order to correctly apply trim-end at a
   * fragment boundary.
   *
   * This record is used to manage the state of these "trial reflows", and
   * return results from the final trial.
   */
  struct TrialReflowState {
    // Values pre-computed at start of Reflow(), constant across trials.
    const nscoord mConsumedBSize;
    const nscoord mEffectiveContentBoxBSize;
    bool mNeedFloatManager;
    // [out] Whether reflowing resulted in use of an overflow-wrap break.
    bool mUsedOverflowWrap = false;
    // Settings for the current trial.
    bool mBalancing = false;
    // Set when we are auto line clamping, driven by
    // ReflowInput::mFlags::mDimensionsReadyForLineClampAuto
    bool mLineClamping = false;
    nscoord mInset = 0;
    // Results computed during the trial reflow. Values from the final trial
    // will be used by the remainder of Reflow().
    mozilla::OverflowAreas mOcBounds;
    mozilla::OverflowAreas mFcBounds;
    nscoord mBlockEndEdgeOfChildren = 0;
    nscoord mContainerWidth = 0;

    // Initialize for the initial trial reflow, with zero inset.
    TrialReflowState(nscoord aConsumedBSize, nscoord aEffectiveContentBoxBSize,
                     bool aNeedFloatManager)
        : mConsumedBSize(aConsumedBSize),
          mEffectiveContentBoxBSize(aEffectiveContentBoxBSize),
          mNeedFloatManager(aNeedFloatManager) {}

    // Re-initialize state that the reflow loop will compute.
    void Reset() {
      mOcBounds.Clear();
      mFcBounds.Clear();
      mBlockEndEdgeOfChildren = 0;
      mContainerWidth = 0;
      mUsedOverflowWrap = false;
    }

    // Adjust the inset amount, and reset state for a new trial.
    void ResetForBalance(nscoord aInsetDelta) {
      // Tells the reflow-lines loop we must consider all lines "dirty" (as we
      // are modifying the effective inline-size to be used).
      mBalancing = true;
      // Adjust inset to apply.
      mInset += aInsetDelta;
      Reset();
    }
  };

  /**
   * Internal helper for Reflow(); may be called repeatedly during a single
   * Reflow() in order to implement text-wrap:balance and text-box-trim on
   * fragmented boxes.
   *
   * For text-wrap: balance, this method applies aTrialState.mInset during
   * line-breaking to reduce the effective available inline-size (without
   * affecting alignment).
   *
   * For text-box-trim, this method performs an additional trial if
   * requested by the frame if it requires trimming on the block end side
   * of the current fragment of the frame.
   */
  nsReflowStatus TrialReflow(nsPresContext* aPresContext,
                             ReflowOutput& aMetrics,
                             const ReflowInput& aReflowInput,
                             TrialReflowState& aTrialState);

 public:
  /**
   * Helper function for the frame ctor to register a ::marker frame.
   */
  void SetMarkerFrameForListItem(nsIFrame* aMarkerFrame);

  /**
   * Does all the real work for removing aDeletedFrame
   * -- finds the line containing aDeletedFrame
   * -- removes all aDeletedFrame next-in-flows (or all continuations,
   * if REMOVE_FIXED_CONTINUATIONS is given)
   * -- marks lines dirty as needed
   * -- marks textruns dirty (unless FRAMES_ARE_EMPTY is given, in which
   * case textruns do not need to be dirtied)
   * -- destroys all removed frames
   */
  enum { REMOVE_FIXED_CONTINUATIONS = 0x02, FRAMES_ARE_EMPTY = 0x04 };
  void DoRemoveFrame(DestroyContext&, nsIFrame* aDeletedFrame, uint32_t aFlags);

  void ReparentFloats(nsIFrame* aFirstFrame, nsBlockFrame* aOldParent,
                      bool aReparentSiblings);

  bool ComputeCustomOverflow(mozilla::OverflowAreas&) override;

  void UnionChildOverflow(mozilla::OverflowAreas&, bool aAsIfScrolled) override;

  /**
   * Load all of aFrame's floats into the float manager iff aFrame is not a
   * block formatting context. Handles all necessary float manager translations;
   * assumes float manager is in aFrame's parent's coord system.
   *
   * Safe to call on non-blocks (does nothing).
   */
  static void RecoverFloatsFor(nsIFrame* aFrame, nsFloatManager& aFloatManager,
                               mozilla::WritingMode aWM,
                               const nsSize& aContainerSize);

  /**
   * Determine if we have any pushed floats from a previous continuation.
   *
   * @returns true, if any of the floats at the beginning of our floats list
   *          have the NS_FRAME_IS_PUSHED_OUT_OF_FLOW bit set; false otherwise.
   */
  bool HasPushedFloatsFromPrevContinuation() const;

  // @see nsIFrame::AddSizeOfExcludingThisForTree
  void AddSizeOfExcludingThisForTree(nsWindowSizes&) const override;

  /**
   * Clears any -webkit-line-clamp ellipsis on a line in this block or one
   * of its descendants.
   */
  bool ClearLineClampEllipsis();

  /**
   * Returns whether this block is in a -webkit-line-clamp context. That is,
   * whether this block is in a block formatting-context whose root block has
   * -webkit-line-clamp: <n>.
   */
  bool IsInLineClampContext() const { return !!GetLineClampRoot(); }

  const mozilla::StyleBlockEllipsis* GetLineClampBlockEllipsis() const;

  /**
   * @return false iff this block does not have a float on any child list.
   * This function is O(1).
   */
  bool HasAnyFloats() const;

  /**
   * This indicates that exactly one line in this block has the
   * LineClampEllipsis flag set, and that such a line must be found
   * and have that flag cleared when reflowing this element's nearest legacy box
   * container.
   */
  bool HasLineClampEllipsis() const {
    return HasAnyStateBits(NS_BLOCK_HAS_LINE_CLAMP_ELLIPSIS);
  }
  /**
   * This indicates that we have a descendant in our block formatting context
   * that has such a line.
   */
  bool HasLineClampEllipsisDescendant() const {
    return HasAnyStateBits(NS_BLOCK_HAS_LINE_CLAMP_ELLIPSIS_DESCENDANT);
  }
  void SetHasLineClampEllipsis(bool aValue) {
    AddOrRemoveStateBits(NS_BLOCK_HAS_LINE_CLAMP_ELLIPSIS, aValue);
  }
  void SetHasLineClampEllipsisDescendant(bool aValue) {
    AddOrRemoveStateBits(NS_BLOCK_HAS_LINE_CLAMP_ELLIPSIS_DESCENDANT, aValue);
  }

 protected:
  struct LineClampTarget {
    nsBlockFrame* targetFrame;
    nsLineBox* targetLine;
    nscoord clampedBSize;

    bool IsFullyClampedOut(nsBlockFrame* aRootFrame, nsBlockFrame* aThisFrame) {
      MOZ_ASSERT(aRootFrame);
      if (aThisFrame != aRootFrame) {
        return false;
      }
      return !clampedBSize && !targetFrame && !targetLine;
    }
  };
  nsBlockFrame* GetLineClampRoot() const;
  Maybe<LineClampTarget> FindLineClampAutoTarget(
      const ReflowInput& aReflowInput, nsBlockFrame* aLineClampRoot);
  Maybe<LineClampTarget> FindLineClampNumberedTarget(
      nscoord aCollapsingBEndMargin, nsBlockFrame* aLineClampRoot) const;
  void ApplyLineClamp(LineClampTarget aLineClampTarget,
                      nsBlockFrame* aLineClampRoot);
  // Helper for calling ApplyLineClamp with automatic and line-based sizing.
  Maybe<nscoord> ApplySmallestLineClamp(
      Maybe<nsBlockFrame::LineClampTarget> aLineClampAutoTarget,
      Maybe<nsBlockFrame::LineClampTarget> aLineClampNumberedTarget,
      nsBlockFrame* aLineClampRoot);

  struct LineClampAutoInfo {
    Maybe<nscoord> lineClampRootMaxHeight = Nothing();
    bool blockIsFullyClampedOut = false;
  };

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(LineClampAutoData, LineClampAutoInfo);
  void SetLineClampAutoClampedToZero() {
    GetOrCreateDeletableProperty(LineClampAutoData())->blockIsFullyClampedOut =
        true;
  }
  bool LineClampIsClampedToZero() {
    bool found = false;
    const LineClampAutoInfo* prop = GetProperty(LineClampAutoData(), &found);
    return found && prop->blockIsFullyClampedOut;
  }
  void ClearLineClampAutoClampedToZero() {
    bool found = false;
    LineClampAutoInfo* currentInfo = GetProperty(LineClampAutoData(), &found);
    if (!found) {
      return;
    }
    if (!currentInfo->lineClampRootMaxHeight) {
      RemoveProperty(LineClampAutoData());
      return;
    }
    currentInfo->blockIsFullyClampedOut = false;
  }
  void SetLineClampRootMaxHeight(nscoord aHeight) {
    GetOrCreateDeletableProperty(LineClampAutoData())->lineClampRootMaxHeight =
        mozilla::Some(aHeight);
  }
  Maybe<nscoord> GetLineClampRootMaxHeight() {
    bool found = false;
    LineClampAutoInfo* prop = GetProperty(LineClampAutoData(), &found);
    if (!found) {
      return Nothing();
    }
    return prop->lineClampRootMaxHeight;
  }
  void ClearLineClampRootMaxHeight() {
    bool found = false;
    LineClampAutoInfo* info = GetProperty(LineClampAutoData(), &found);
    if (!found) {
      return;
    }
    if (!info->blockIsFullyClampedOut) {
      RemoveProperty(LineClampAutoData());
      return;
    }
    info->lineClampRootMaxHeight = Nothing();
  }

  /** grab overflow lines from this block's prevInFlow, and make them
   * part of this block's mLines list.
   * @return true if any lines were drained.
   */
  bool DrainOverflowLines();

  /**
   * Moves frames from our PushedFloats list back into our mFloats list.
   */
  void DrainSelfPushedFloats();

  /**
   * First calls DrainSelfPushedFloats() then grabs pushed floats from this
   * block's prev-in-flow, and splice them into this block's mFloats list too.
   */
  void DrainPushedFloats();

  /** Load all our floats into the float manager (without reflowing them).
   *  Assumes float manager is in our own coordinate system.
   */
  void RecoverFloats(nsFloatManager& aFloatManager, mozilla::WritingMode aWM,
                     const nsSize& aContainerSize);

  /** Reflow pushed floats
   */
  void ReflowPushedFloats(BlockReflowState& aState,
                          mozilla::OverflowAreas& aOverflowAreas);

  /**
   * Reflow absolutely positioned descendants of inline frames that serve as
   * absolute containing blocks in our lines. Must be called after we reflow all
   * the lines.
   */
  void ReflowAbsoluteDescendantsInInlineFrame(nsPresContext* aPresContext,
                                              const ReflowInput& aReflowInput,
                                              ReflowOutput& aReflowOutput,
                                              nsReflowStatus& aStatus);

  /**
   * Helper for ReflowAbsoluteDescendantsInInlineFrame(). Recursively visit
   * every inline frame reachable from aFrame via its principal child list, and
   * call ReflowAbsoluteFramesInInlineFrame() on each. Update aFrame's overflow
   * areas with the accumulated overflow areas.
   *
   * @return accumulated overflow areas from abspos descendants visited under
   *         aFrame, in aFrame's coordinate space. Or if no abspos descendants
   *         were visited, return Nothing().
   */
  mozilla::Maybe<mozilla::OverflowAreas>
  WalkInlineDescendantsToReflowAbsoluteFrames(nsIFrame* aFrame,
                                              nsPresContext* aPresContext,
                                              const ReflowInput& aReflowInput,
                                              const ReflowOutput& aReflowOutput,
                                              nsReflowStatus& aStatus);

  /**
   * Helper for WalkInlineDescendantsToReflowAbsoluteFrames(). Reflow the
   * absolutely positioned frames if aInlineFrame is an absolute containing
   * block.
   *
   * @param aInlineFrame inline-level frame that forms the absolute containing
   *        block.
   * @param aReflowOutput this block frame's reflow output, used to get the
   *        container size for logical coordinate conversions (the block frame's
   *        own size is not yet set at this point in its reflow).
   * @return overflow areas from aInlineFrame's abspos kids, in
   *         aInlineFrame's coordinate space. Or if no abspos descendants were
   *         visited, return Nothing().
   */
  mozilla::Maybe<mozilla::OverflowAreas> ReflowAbsoluteFramesInInlineFrame(
      nsInlineFrame* aInlineFrame, nsPresContext* aPresContext,
      const ReflowInput& aReflowInput, const ReflowOutput& aReflowOutput,
      nsReflowStatus& aStatus);

  /**
   * Find any trailing BR clear from the last line of this block (or from its
   * prev-in-flows).
   */
  mozilla::UsedClear FindTrailingClear();

  /**
   * Remove a float from our float list.
   */
  void RemoveFloat(nsIFrame* aFloat);
  /**
   * Remove a float from the float cache for the line its placeholder is on.
   */
  void RemoveFloatFromFloatCache(nsIFrame* aFloat);

  void CollectFloats(nsIFrame* aFrame, nsFrameList& aList,
                     bool aCollectFromSiblings) {
    if (HasAnyFloats()) {
      DoCollectFloats(aFrame, aList, aCollectFromSiblings);
    }
  }
  void DoCollectFloats(nsIFrame* aFrame, nsFrameList& aList,
                       bool aCollectFromSiblings);

  // Remove a float and its continuations.
  static void DoRemoveFloats(DestroyContext&, nsIFrame*);

  /** set up the conditions necessary for an resize reflow
   * the primary task is to mark the minimumly sufficient lines dirty.
   */
  void PrepareResizeReflow(BlockReflowState& aState);

  /**
   * Reflow all lines that have been marked dirty.
   * Returns whether an overflow-wrap break was used anywhere.
   */
  bool ReflowDirtyLines(BlockReflowState& aState);

  /** Mark a given line dirty due to reflow being interrupted on or before it */
  void MarkLineDirtyForInterrupt(nsLineBox* aLine);

  //----------------------------------------
  // Methods for line reflow
  /**
   * Reflow a line.
   *
   * @param aState
   *   the current reflow input
   * @param aLine
   *   the line to reflow.  can contain a single block frame or contain 1 or
   *   more inline frames.
   * @param aKeepReflowGoing [OUT]
   *   indicates whether the caller should continue to reflow more lines
   * @returns
   *   whether an overflow-wrap breakpoint was used
   */
  bool ReflowLine(BlockReflowState& aState, LineIterator aLine,
                  bool* aKeepReflowGoing);

  // Return false if it needs another reflow because of reduced space
  // between floats that are next to it (but not next to its top), and
  // return true otherwise.
  bool PlaceLine(BlockReflowState& aState, nsLineLayout& aLineLayout,
                 LineIterator aLine,
                 nsFloatManager::SavedState* aFloatStateBeforeLine,
                 nsFlowAreaRect& aFlowArea,      // in-out
                 nscoord& aAvailableSpaceBSize,  // in-out
                 bool* aKeepReflowGoing);

  /**
   * If NS_BLOCK_LOOK_FOR_DIRTY_FRAMES is set, call MarkLineDirty
   * on any line with a child frame that is dirty.
   */
  void LazyMarkLinesDirty();

  /**
   * Mark |aLine| dirty, and, if necessary because of possible
   * pull-up, mark the previous line dirty as well. Also invalidates textruns
   * on those lines because the text in the lines might have changed due to
   * addition/removal of frames.
   * @param aLine the line to mark dirty
   * @param aLineList the line list containing that line
   */
  void MarkLineDirty(LineIterator aLine, const nsLineList* aLineList);

  // Determines if the given line is the last line in a run of inline lines.
  // Used for text-align-last.
  bool IsLastInlineLine(LineIterator aLine);

  // Determines if the given line is the last formatted line of the block.
  // Used for text-box-trim.
  bool IsLastFormattedLine(LineIterator aLine);

  void DeleteLine(BlockReflowState& aState, nsLineList::iterator aLine,
                  nsLineList::iterator aLineEnd);

  //----------------------------------------
  // Methods for individual frame reflow

  bool ShouldApplyBStartMargin(BlockReflowState& aState, nsLineBox* aLine);

  void ReflowBlockFrame(BlockReflowState& aState, LineIterator aLine,
                        bool* aKeepGoing);

  // Returns whether an overflow-wrap break was used.
  bool ReflowInlineFrames(BlockReflowState& aState, LineIterator aLine,
                          bool* aKeepLineGoing);

  void DoReflowInlineFrames(
      BlockReflowState& aState, nsLineLayout& aLineLayout, LineIterator aLine,
      nsFlowAreaRect& aFloatAvailableSpace, nscoord& aAvailableSpaceBSize,
      nsFloatManager::SavedState* aFloatStateBeforeLine, bool* aKeepReflowGoing,
      LineReflowStatus* aLineReflowStatus, bool aAllowPullUp);

  void ReflowInlineFrame(BlockReflowState& aState, nsLineLayout& aLineLayout,
                         LineIterator aLine, nsIFrame* aFrame,
                         LineReflowStatus* aLineReflowStatus);

  // @param aReflowStatus an incomplete status indicates the float should be
  //        split but only if the available block-size is constrained.
  void ReflowFloat(BlockReflowState& aState, ReflowInput& aFloatRI,
                   nsIFrame* aFloat, nsReflowStatus& aReflowStatus);

  //----------------------------------------
  // Methods for pushing/pulling lines/frames

  /**
   * Create a next-in-flow, if necessary, for aFrame. If a new frame is
   * created, place it in aLine if aLine is not null.
   * @param aState the block reflow state
   * @param aLine where to put a new frame
   * @param aFrame the frame
   * @return true if a new frame was created, false if not
   */
  bool CreateContinuationFor(BlockReflowState& aState, nsLineBox* aLine,
                             nsIFrame* aFrame);

  /**
   * Set line-break-before status in aState.mReflowStatus because aLine cannot
   * be placed on this page/column and we don't want to break within ourselves.
   * Also, mark the aLine dirty, and set aKeepReflowGoing to false;
   */
  void SetBreakBeforeStatusBeforeLine(BlockReflowState& aState,
                                      LineIterator aLine,
                                      bool* aKeepReflowGoing);

  /**
   * Indicates if we need to compute a page name for the next page when pushing
   * a truncated line.
   *
   * Using a value of No saves work when a new page name has already been set
   * with nsCSSFrameConstructor::SetNextPageContentFramePageName.
   */
  enum class ComputeNewPageNameIfNeeded : uint8_t { Yes, No };

  /**
   * Push aLine (and any after it), since it cannot be placed on this
   * page/column.  Set aKeepReflowGoing to false and set
   * flag aState.mReflowStatus as incomplete.
   */
  void PushTruncatedLine(BlockReflowState& aState, LineIterator aLine,
                         bool* aKeepReflowGoing,
                         ComputeNewPageNameIfNeeded aComputeNewPageName =
                             ComputeNewPageNameIfNeeded::Yes);

  void SplitLine(BlockReflowState& aState, nsLineLayout& aLineLayout,
                 LineIterator aLine, nsIFrame* aFrame,
                 LineReflowStatus* aLineReflowStatus);

  /**
   * Pull a frame from the next available location (one of our lines or
   * one of our next-in-flows lines).
   * @return the pulled frame or nullptr
   */
  nsIFrame* PullFrame(BlockReflowState& aState, LineIterator aLine);

  /**
   * Try to pull a frame out of a line pointed at by aFromLine.
   *
   * Note: pulling a frame from a line that is a place-holder frame
   * doesn't automatically remove the corresponding float from the
   * line's float array. This happens indirectly: either the line gets
   * emptied (and destroyed) or the line gets reflowed (because we mark
   * it dirty) and the code at the top of ReflowLine empties the
   * array. So eventually, it will be removed, just not right away.
   *
   * @return the pulled frame or nullptr
   */
  nsIFrame* PullFrameFrom(nsLineBox* aLine, nsBlockFrame* aFromContainer,
                          nsLineList::iterator aFromLine);

  /**
   * Push the line after aLineBefore to the overflow line list.
   * @param aLineBefore a line in 'mLines' (or LinesBegin() when
   *        pushing the first line)
   */
  void PushLines(BlockReflowState& aState, nsLineList::iterator aLineBefore);

  void PropagateFloatDamage(BlockReflowState& aState, nsLineBox* aLine,
                            nscoord aDeltaBCoord);

  void CheckFloats(BlockReflowState& aState);

  //----------------------------------------
  // List handling kludge

  void ReflowOutsideMarker(nsIFrame* aMarkerFrame, BlockReflowState& aState,
                           ReflowOutput& aMetrics, nscoord aLineTop);

  //----------------------------------------

  NS_DECLARE_FRAME_PROPERTY_DELETABLE(LineIteratorProperty, nsLineIterator);

  bool CanProvideLineIterator() const final { return true; }
  nsILineIterator* GetLineIterator() final;

 public:
  bool HasOverflowLines() const {
    return HasAnyStateBits(NS_BLOCK_HAS_OVERFLOW_LINES);
  }
  FrameLines* GetOverflowLines() const;

 protected:
  FrameLines* RemoveOverflowLines();
  void SetOverflowLines(FrameLines* aOverflowLines);
  void DestroyOverflowLines();

  /**
   * This class is useful for efficiently modifying the overflow floats list. It
   * gives the client direct writable access to the frame list temporarily but
   * ensures that property is only written back if absolutely necessary.
   */
  struct AutoOverflowFloatsList {
    nsFrameList mList;

    explicit AutoOverflowFloatsList(nsBlockFrame* aBlock)
        : mPropValue(aBlock->GetOverflowFloats()), mBlock(aBlock) {
      if (mPropValue) {
        mList = std::move(*mPropValue);
      }
    }
    ~AutoOverflowFloatsList() {
      mBlock->SetOverflowFloats(std::move(mList), mPropValue);
    }

   protected:
    nsFrameList* const mPropValue;
    nsBlockFrame* const mBlock;
  };
  friend struct AutoOverflowFloatsList;

  // Return true if this frame has overflow floats.
  bool HasOverflowFloats() const;

  nsFrameList* GetOverflowFloats() const;

  // This takes ownership of the frames in aList.
  void SetOverflowFloats(nsFrameList&& aList, nsFrameList* aPropValue);

  // Return the ::marker frame or nullptr if we don't have one.
  nsIFrame* GetMarker() const {
    nsIFrame* outside = GetOutsideMarker();
    return outside ? outside : GetInsideMarker();
  }

  // Return the inside ::marker frame or nullptr if we don't have one.
  nsIFrame* GetInsideMarker() const;

  //  Return the outside ::marker frame or nullptr if we don't have one.
  nsIFrame* GetOutsideMarker() const;

  // Return the outside ::marker frame list frame property.
  nsFrameList* GetOutsideMarkerList() const;

  // Return true if this frame has floats.
  bool HasFloats() const;

  // Get the floats list, or nullptr if there isn't one.
  nsFrameList* GetFloats() const;

  // Get the floats list, or if there is not currently one, make a new empty
  // one.
  nsFrameList* EnsureFloats() MOZ_NONNULL_RETURN;

  // Get the float list and remove the property from this frame.
  //
  // The caller is responsible for deleting the returned list and managing the
  // ownership of all frames in the list.
  [[nodiscard]] nsFrameList* StealFloats();

  // Return true if this frame has pushed floats.
  bool HasPushedFloats() const;

  // Get the pushed floats list, or nullptr if there isn't one.
  //
  // The pushed floats list is used for *temporary* storage of floats during
  // reflow, between when we decide they don't fit in this block until our next
  // continuation takes them.
  nsFrameList* GetPushedFloats() const;

  // Get the pushed floats list, or if there is not currently one,
  // make a new empty one.
  nsFrameList* EnsurePushedFloats() MOZ_NONNULL_RETURN;

  // Get the pushed float list and remove the property from this frame.
  //
  // The caller is responsible for deleting the returned list and managing the
  // ownership of all frames in the list.
  [[nodiscard]] nsFrameList* StealPushedFloats();

#ifdef DEBUG
  void VerifyLines(bool aFinalCheckOK);
  void VerifyOverflowSituation();
  int32_t GetDepth() const;
#endif

  mozilla::IntrinsicISizesCache mCachedIntrinsics;

  nsLineList mLines;

  friend class mozilla::BlockReflowState;
  friend class nsBlockInFlowLineIterator;

#ifdef DEBUG
 public:
  static bool gLamePaintMetrics;
  static bool gLameReflowMetrics;
  static bool gNoisy;
  static bool gNoisyDamageRepair;
  static bool gNoisyIntrinsic;
  static bool gNoisyReflow;
  static bool gReallyNoisyReflow;
  static bool gNoisyFloatManager;
  static bool gVerifyLines;
  static bool gDisableResizeOpt;

  static int32_t gNoiseIndent;

 protected:
  static void InitDebugFlags();
#endif
};

#ifdef DEBUG
class AutoNoisyIndenter {
 public:
  explicit AutoNoisyIndenter(bool aDoIndent) : mIndented(aDoIndent) {
    if (mIndented) {
      nsBlockFrame::gNoiseIndent++;
    }
  }
  ~AutoNoisyIndenter() {
    if (mIndented) {
      nsBlockFrame::gNoiseIndent--;
    }
  }

 private:
  bool mIndented;
};
#endif

/**
 * Iterates over all lines in the prev-in-flows/next-in-flows of this block.
 */
class nsBlockInFlowLineIterator {
 public:
  typedef nsBlockFrame::LineIterator LineIterator;
  /**
   * Set up the iterator to point to aLine which must be a normal line
   * in aFrame (not an overflow line).
   */
  nsBlockInFlowLineIterator(nsBlockFrame* aFrame, LineIterator aLine);
  /**
   * Set up the iterator to point to the first line found starting from
   * aFrame. Sets aFoundValidLine to false if there is no such line.
   * After aFoundValidLine has returned false, don't call any methods on this
   * object again.
   */
  nsBlockInFlowLineIterator(nsBlockFrame* aFrame, bool* aFoundValidLine);
  /**
   * Set up the iterator to point to the line that contains aFindFrame (either
   * directly or indirectly).  If aFrame is out of flow, or contained in an
   * out-of-flow, finds the line containing the out-of-flow's placeholder. If
   * the frame is not found, sets aFoundValidLine to false. After
   * aFoundValidLine has returned false, don't call any methods on this
   * object again.
   */
  nsBlockInFlowLineIterator(nsBlockFrame* aFrame, nsIFrame* aFindFrame,
                            bool* aFoundValidLine);

  // Allow to be uninitialized (and then assigned from another object).
  nsBlockInFlowLineIterator() : mFrame(nullptr) {}

  LineIterator GetLine() { return mLine; }
  bool IsLastLineInList();
  nsBlockFrame* GetContainer() { return mFrame; }
  bool GetInOverflow() { return mLineList != &mFrame->mLines; }

  /**
   * Returns the current line list we're iterating, null means
   * we're iterating |mLines| of the container.
   */
  nsLineList* GetLineList() { return mLineList; }

  /**
   * Returns the end-iterator of whatever line list we're in.
   */
  LineIterator End();

  /**
   * Returns false if there are no more lines. After this has returned false,
   * don't call any methods on this object again.
   */
  bool Next();
  /**
   * Returns false if there are no more lines. After this has returned false,
   * don't call any methods on this object again.
   */
  bool Prev();

  // XXX nsBlockFrame uses this internally in one place.  Try to remove it.
  // XXX uhm, and nsBidiPresUtils::Resolve too.
  nsBlockInFlowLineIterator(nsBlockFrame* aFrame, LineIterator aLine,
                            bool aInOverflow);

 private:
  nsBlockFrame* mFrame;
  LineIterator mLine;
  nsLineList* mLineList;  // the line list mLine is in

  /**
   * Moves iterator to next valid line reachable from the current block.
   * Returns false if there are no valid lines.
   */
  bool FindValidLine();
};

#endif /* nsBlockFrame_h_ */
