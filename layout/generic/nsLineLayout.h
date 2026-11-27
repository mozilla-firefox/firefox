/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* state and methods used while laying out a single line of a block frame */

#ifndef nsLineLayout_h_
#define nsLineLayout_h_

#include "BlockReflowState.h"
#include "JustificationUtils.h"
#include "gfxTextRun.h"
#include "gfxTypes.h"
#include "mozilla/ArenaAllocator.h"
#include "mozilla/WritingModes.h"
#include "nsLineBox.h"

class nsFloatManager;
struct nsStyleText;

class nsLineLayout {
  using BlockReflowState = mozilla::BlockReflowState;
  using ReflowInput = mozilla::ReflowInput;
  using ReflowOutput = mozilla::ReflowOutput;

 public:
  /**
   * @param aBaseLineLayout the nsLineLayout for ruby base,
   * nullptr if no separate base nsLineLayout is needed.
   */
  nsLineLayout(nsPresContext* aPresContext, nsFloatManager* aFloatManager,
               const ReflowInput& aLineContainerRI,
               const nsLineList::iterator* aLine,
               nsLineLayout* aBaseLineLayout);

  ~nsLineLayout() {
    MOZ_COUNT_DTOR(nsLineLayout);
    MOZ_ASSERT(!mRootSpan, "bad line-layout user");
  }

  void Init(BlockReflowState* aState, nscoord aMinLineBSize,
            int32_t aLineNumber) {
    mBlockRS = aState;
    mMinLineBSize = aMinLineBSize;
    mLineNumber = aLineNumber;
  }

  int32_t GetLineNumber() const { return mLineNumber; }

  void BeginLineReflow(nscoord aICoord, nscoord aBCoord, nscoord aISize,
                       nscoord aBSize, bool aImpactedByFloats,
                       bool aIsTopOfPage, mozilla::WritingMode aWritingMode,
                       const nsSize& aContainerSize,
                       // aInset is used during text-wrap:balance to reduce
                       // the effective available space on the line.
                       nscoord aInset = 0);

  /**
   * Returns true if the line had to use an overflow-wrap break position.
   */
  bool EndLineReflow();

  /**
   * Called when a float has been placed. This method updates the
   * inline frame and span data to account for any change in positions
   * due to available space for the line boxes changing.
   * @param aX/aY/aWidth/aHeight are the new available
   * space rectangle, relative to the containing block.
   * @param aFloatFrame the float frame that was placed.
   */
  void UpdateBand(mozilla::WritingMode aWM,
                  const mozilla::LogicalRect& aNewAvailableSpace,
                  nsIFrame* aFloatFrame);

  void BeginSpan(nsIFrame* aFrame, const ReflowInput* aSpanReflowInput,
                 nscoord aLeftEdge, nscoord aRightEdge, nscoord* aBaseline);

  // Returns the width of the span
  nscoord EndSpan(nsIFrame* aFrame);

  // This method attaches the last frame reflowed in this line layout
  // to that in the base line layout.
  void AttachLastFrameToBaseLineLayout() {
    AttachFrameToBaseLineLayout(LastFrame());
  }

  // This method attaches the root frame of this line layout to the
  // last reflowed frame in the base line layout.
  void AttachRootFrameToBaseLineLayout() {
    AttachFrameToBaseLineLayout(mRootSpan->mFrame);
  }

  int32_t GetCurrentSpanCount() const;

  void SplitLineTo(int32_t aNewCount);

  bool IsZeroBSize() const;

  // Reflows the frame and returns the reflow status. aPushedFrame is true
  // if the frame is pushed to the next line because it doesn't fit.
  void ReflowFrame(nsIFrame* aFrame, nsReflowStatus& aReflowStatus,
                   ReflowOutput* aMetrics, bool& aPushedFrame);

  void AddMarkerFrame(nsIFrame* aFrame, const ReflowOutput& aMetrics);

  void RemoveMarkerFrame(nsIFrame* aFrame);

  /**
   * Place frames in the block direction (CSS property vertical-align).
   * Also performs text box trimming if requested on the start edge
   * or on the end edge (if aIsLastFormattedLine is true).
   */
  void VerticalAlignLine(nsFlowAreaRect* aFlowArea = nullptr,
                         bool aIsLastFormattedLine = false);

  bool TrimTrailingWhiteSpace();

  /**
   * Place frames in the inline direction (CSS property text-align).
   */
  void TextAlignLine(nsLineBox* aLine, bool aIsLastLine);

  /**
   * Handle all the relative positioning in the line, compute the
   * combined area (== overflow area) for the line, and handle view
   * sizing/positioning and the setting of the overflow rect.
   */
  void RelativePositionFrames(mozilla::OverflowAreas& aOverflowAreas) {
    RelativePositionFrames(mRootSpan, aOverflowAreas);
  }

  // Support methods for word-wrapping during line reflow

  void SetJustificationInfo(const mozilla::JustificationInfo& aInfo) {
    mJustificationInfo = aInfo;
  }

  /**
   * @return true if so far during reflow no non-empty content has been
   * placed in the line (according to nsIFrame::IsEmpty())
   */
  bool LineIsEmpty() const { return mLineIsEmpty; }

  /**
   * @return true if so far during reflow no non-empty leaf content
   * (non-collapsed whitespace, replaced element, inline-block, etc) has been
   * placed in the line
   */
  bool LineAtStart() const { return mLineAtStart; }

  bool LineIsBreakable() const {
    // XXX mTotalPlacedFrames should go away and we should just use
    // mLineIsEmpty here instead
    return mTotalPlacedFrames || mImpactedByFloats;
  }

  bool GetLineEndsInBR() const { return mLineEndsInBR; }
  void SetLineEndsInBR(bool aOn) { mLineEndsInBR = aOn; }

  //----------------------------------------
  // Inform the line-layout about the presence of a floating frame
  // XXX get rid of this: use get-frame-type?
  bool AddFloat(nsIFrame* aFloat, nscoord aAvailableISize) {
    // When reflowing ruby text frames, no block reflow state is
    // provided to the line layout. However, floats should never be
    // associated with ruby text containers, hence this method should
    // not be called in that case.
    MOZ_ASSERT(mBlockRS,
               "Should not call this method if there is no block reflow state "
               "available");
    return mBlockRS->AddFloat(this, aFloat, aAvailableISize);
  }

  void SetTrimmableISize(nscoord aTrimmableISize) {
    mTrimmableISize = aTrimmableISize;
  }

  //----------------------------------------

  bool GetFirstLetterStyleOK() const { return mFirstLetterStyleOK; }
  void SetFirstLetterStyleOK(bool aSetting) { mFirstLetterStyleOK = aSetting; }

  bool GetInFirstLetter() const { return mInFirstLetter; }
  void SetInFirstLetter(bool aSetting) { mInFirstLetter = aSetting; }

  bool GetInFirstLine() const { return mInFirstLine; }
  void SetInFirstLine(bool aSetting) { mInFirstLine = aSetting; }

  // Calling this during block reflow ensures that the next line of inlines
  // will be marked dirty, if there is one.
  void SetDirtyNextLine() { mDirtyNextLine = true; }
  bool GetDirtyNextLine() const { return mDirtyNextLine; }

  //----------------------------------------

  /**
   * Record where an optional break could have been placed. During line reflow,
   * frames containing optional break points (e.g., whitespace in text frames)
   * can call SetLastOptionalBreakPosition to record where a break could
   * have been made, but wasn't because we decided to place more content on
   * the line. For non-text frames, offset 0 means before the frame, offset
   * INT32_MAX means after the frame.
   *
   * Currently this is used to handle cases where a single word comprises
   * multiple frames, and the first frame fits on the line but the whole word
   * doesn't. We look back to the last optional break position and
   * reflow the whole line again, forcing a break at that position. The last
   * optional break position could be in a text frame or else after a frame
   * that cannot be part of a text run, so those are the positions we record.
   *
   * @param aFrame the frame which contains the optional break position.
   *
   * @param aFits set to true if the break position is within the available
   * width.
   *
   * @param aPriority the priority of the break opportunity. If we are
   * prioritizing break opportunities, we will not set a break if we have
   * already set a break with a higher priority. @see gfxBreakPriority.
   *
   * @return true if we are actually reflowing with forced break position and we
   * should break here
   */
  bool NotifyOptionalBreakPosition(nsIFrame* aFrame, int32_t aOffset,
                                   bool aFits, gfxBreakPriority aPriority);

  // Tries to place a float, and records whether the float actually was placed.
  bool TryToPlaceFloat(nsIFrame* aFloat);

  // Records a floating frame in a nowrap context for it to be placed on the
  // next break opportunity.
  void RecordNoWrapFloat(nsIFrame* aFloat);

  // Tries to place the floats from the nowrap context.
  void FlushNoWrapFloats();

  /**
   * Like NotifyOptionalBreakPosition, but here it's OK for mNeedBackup
   * to be set, because the caller is merely pruning some saved break
   * position(s) that are actually not feasible.
   */
  void RestoreSavedBreakPosition(nsIFrame* aFrame, int32_t aOffset,
                                 gfxBreakPriority aPriority) {
    mLastOptionalBreakFrame = aFrame;
    mLastOptionalBreakFrameOffset = aOffset;
    mLastOptionalBreakPriority = aPriority;
  }
  /**
   * Signal that no backing up will be required after all.
   */
  void ClearOptionalBreakPosition() {
    mNeedBackup = false;
    mLastOptionalBreakFrame = nullptr;
    mLastOptionalBreakFrameOffset = -1;
    mLastOptionalBreakPriority = gfxBreakPriority::eNoBreak;
  }
  // Retrieve last set optional break position. When this returns null, no
  // optional break has been recorded (which means that the line can't break
  // yet).
  nsIFrame* GetLastOptionalBreakPosition(int32_t* aOffset,
                                         gfxBreakPriority* aPriority) {
    *aOffset = mLastOptionalBreakFrameOffset;
    *aPriority = mLastOptionalBreakPriority;
    return mLastOptionalBreakFrame;
  }
  // Whether any optional break position has been recorded.
  bool HasOptionalBreakPosition() const {
    return mLastOptionalBreakFrame != nullptr;
  }
  // Get the priority of the last optional break position recorded.
  gfxBreakPriority LastOptionalBreakPriority() const {
    return mLastOptionalBreakPriority;
  }

  /**
   * Check whether frames overflowed the available width and CanPlaceFrame
   * requested backing up to a saved break position.
   */
  bool NeedsBackup() { return mNeedBackup; }

  // Line layout may place too much content on a line, overflowing its available
  // width. When that happens, if SetLastOptionalBreakPosition has been
  // used to record an optional break that wasn't taken, we can reflow the line
  // again and force the break to happen at that point (i.e., backtracking
  // to the last choice point).

  // Record that we want to break at the given content+offset (which
  // should have been previously returned by GetLastOptionalBreakPosition
  // from another nsLineLayout).
  void ForceBreakAtPosition(nsIFrame* aFrame, int32_t aOffset) {
    mForceBreakFrame = aFrame;
    mForceBreakFrameOffset = aOffset;
  }
  bool HaveForcedBreakPosition() { return mForceBreakFrame != nullptr; }
  int32_t GetForcedBreakPosition(nsIFrame* aFrame) {
    return mForceBreakFrame == aFrame ? mForceBreakFrameOffset : -1;
  }

  /**
   * This can't be null. It usually returns a block frame but may return
   * some other kind of frame when inline frames are reflowed in a non-block
   * context (e.g. MathML or floating first-letter).
   */
  nsIFrame* LineContainerFrame() const { return mLineContainerRI.mFrame; }
  const ReflowInput& LineContainerRI() const { return mLineContainerRI; }
  const nsLineList::iterator* GetLine() const {
    return mGotLineBox ? &mLineBox : nullptr;
  }
  nsLineList::iterator* GetLine() { return mGotLineBox ? &mLineBox : nullptr; }

  /**
   * Returns the accumulated advance width of frames before the current frame
   * on the line, plus the line container's left border+padding.
   * This is always positive, the advance width is measured from
   * the right edge for RTL blocks and from the left edge for LTR blocks.
   * In other words, the current frame's distance from the line container's
   * start content edge is:
   * <code>GetCurrentFrameInlineDistanceFromBlock() -
   * lineContainer->GetUsedBorderAndPadding().left</code> Note the use of
   * <code>.left</code> for both LTR and RTL line containers.
   */
  nscoord GetCurrentFrameInlineDistanceFromBlock();

  /**
   * Move the inline position where the next frame will be reflowed forward by
   * aAmount.
   */
  void AdvanceICoord(nscoord aAmount) { mCurrentSpan->mICoord += aAmount; }
  /**
   * Returns the writing mode for the root span.
   */
  mozilla::WritingMode GetWritingMode() { return mRootSpan->mWritingMode; }
  /**
   * Returns the inline position where the next frame will be reflowed.
   */
  nscoord GetCurrentICoord() { return mCurrentSpan->mICoord; }

  void SetSuppressLineWrap(bool aEnabled) { mSuppressLineWrap = aEnabled; }

  /**
   * Record that the line had to resort to an overflow-wrap break.
   */
  void SetUsedOverflowWrap() { mUsedOverflowWrap = true; }

  /**
   * If trimming was requested on the block-end side of the current line's
   * block (or an ancestor), this returns the amount that the line would be
   * trimmed by.
   */
  nscoord PotentialTextBoxTrimEndAmount() const {
    return mPotentialTextBoxTrimEndAmount;
  }

 protected:
  // This state is constant for a given block frame doing line layout

  nsPresContext* const mPresContext;

  // A non-owning pointer, which points to the object owned by
  // nsAutoFloatManager::mNew.
  nsFloatManager* const mFloatManager;

  const nsStyleText* mStyleText;  // for the block
  const ReflowInput& mLineContainerRI;

  // The line layout for the base text.  It is usually nullptr.
  // It becomes not null when the current line layout is for ruby
  // annotations. When there is nested ruby inside annotation, it
  // forms a linked list from the inner annotation to the outermost
  // line layout. The outermost line layout, which has this member
  // being nullptr, is responsible for managing the life cycle of
  // per-frame data and per-span data, and handling floats.
  nsLineLayout* const mBaseLineLayout;

  nsLineLayout* GetOutermostLineLayout() {
    nsLineLayout* lineLayout = this;
    while (lineLayout->mBaseLineLayout) {
      lineLayout = lineLayout->mBaseLineLayout;
    }
    return lineLayout;
  }

  nsIFrame* mLastOptionalBreakFrame = nullptr;
  nsIFrame* mForceBreakFrame = nullptr;

  // XXX Take care that nsRubyBaseContainer would give nullptr to this
  //     member. It should not be a problem currently, since the only
  //     code use it is handling float, which does not affect ruby.
  //     See comment in nsLineLayout::AddFloat
  BlockReflowState* mBlockRS = nullptr; /* XXX hack! */

  nsLineList::iterator mLineBox;

  // Per-frame data recorded by the line-layout reflow logic. This
  // state is the state needed to post-process the line after reflow
  // has completed (block-direction alignment, inline-direction alignment,
  // justification and relative positioning).
  struct PerSpanData;
  struct PerFrameData {
    // link to next/prev frame in same span
    PerFrameData* mNext;
    PerFrameData* mPrev;

    // Link to the frame of next ruby annotation.  It is a linked list
    // through this pointer from ruby base to all its annotations.  It
    // could be nullptr if there is no more annotation.
    // If PFD_ISLINKEDTOBASE is set, the current PFD is one of the ruby
    // annotations in the base's list, otherwise it is the ruby base,
    // and its mNextAnnotation is the start of the linked list.
    PerFrameData* mNextAnnotation;

    // pointer to child span data if this is an inline container frame
    PerSpanData* mSpan;

    // The frame
    nsIFrame* mFrame;

    // From metrics
    nscoord mAscent;
    // note that mBounds is a logical rect in the *line*'s writing mode.
    // When setting frame coordinates, we have to convert to the frame's
    //  writing mode
    mozilla::LogicalRect mBounds;
    mozilla::OverflowAreas mOverflowAreas;

    // From reflow-state
    mozilla::LogicalMargin mMargin;         // in *line* writing mode
    mozilla::LogicalMargin mBorderPadding;  // in *line* writing mode
    mozilla::LogicalMargin mOffsets;        // in *frame* writing mode

    // state for text justification
    // Note that, although all frames would have correct inner
    // opportunities computed after ComputeFrameJustification, start
    // and end justifiable info are not reliable for non-text frames.
    mozilla::JustificationInfo mJustificationInfo;
    mozilla::JustificationAssignment mJustificationAssignment;

    // PerFrameData flags
    bool mIsRelativelyOrStickyPos : 1;
    bool mIsTextFrame : 1;
    bool mIsNonEmptyTextFrame : 1;
    bool mIsNonWhitespaceTextFrame : 1;
    bool mIsLetterFrame : 1;
    bool mRecomputeOverflow : 1;
    bool mIsMarker : 1;
    bool mSkipWhenTrimmingWhitespace : 1;
    bool mIsEmpty : 1;
    bool mIsPlaceholder : 1;
    bool mIsLinkedToBase : 1;

    // Other state we use
    uint8_t mBlockDirAlign;
    mozilla::WritingMode mWritingMode;

    PerFrameData* Last() {
      PerFrameData* pfd = this;
      while (pfd->mNext) {
        pfd = pfd->mNext;
      }
      return pfd;
    }

    bool IsStartJustifiable() const {
      return mJustificationInfo.mIsStartJustifiable;
    }

    bool IsEndJustifiable() const {
      return mJustificationInfo.mIsEndJustifiable;
    }

    bool ParticipatesInJustification() const;
  };
  PerFrameData* mFrameFreeList = nullptr;

  // In nsLineLayout, a "span" is a container inline frame, and a "frame" is one
  // of its children.
  //
  // nsLineLayout::BeginLineReflow() creates the initial PerSpanData which is
  // called the "root span". nsInlineFrame::ReflowFrames() creates a new
  // PerSpanData when it calls nsLineLayout::BeginSpan(); at this time, the
  // nsLineLayout object's mCurrentSpan is switched to the new span. The new
  // span records the old mCurrentSpan as its parent. After reflowing the child
  // inline frames, nsInlineFrame::ReflowFrames() calls nsLineLayout::EndSpan(),
  // which pops the PerSpanData and re-sets mCurrentSpan.
  struct PerSpanData {
    union {
      PerSpanData* mParent;
      PerSpanData* mNextFreeSpan;
    };

    // The PerFrameData of the inline frame that "owns" the span, or null if
    // this is the root span. mFrame is initialized to the containing inline
    // frame's PerFrameData when a new PerSpanData is pushed in
    // nsLineLayout::BeginSpan().
    PerFrameData* mFrame;

    // The first PerFrameData structure in the span.
    PerFrameData* mFirstFrame;

    // The last PerFrameData structure in the span. PerFrameData structures are
    // added to the span as they are reflowed. mLastFrame may also be directly
    // manipulated if a line is split, or if frames are pushed from one line to
    // the next.
    PerFrameData* mLastFrame;

    const ReflowInput* mReflowInput;
    bool mNoWrap;
    mozilla::WritingMode mWritingMode;
    bool mContainsFloat;
    bool mHasNonemptyContent;

    nscoord mIStart;
    nscoord mICoord;
    nscoord mIEnd;
    nscoord mInset;

    nscoord mBStartLeading, mBEndLeading;
    nscoord mLogicalBSize;
    nscoord mMinBCoord, mMaxBCoord;
    nscoord* mBaseline;

    void AppendFrame(PerFrameData* pfd) {
      if (!mLastFrame) {
        mFirstFrame = pfd;
      } else {
        mLastFrame->mNext = pfd;
        pfd->mPrev = mLastFrame;
      }
      mLastFrame = pfd;
    }
  };
  PerSpanData* mSpanFreeList = nullptr;
  PerSpanData* mRootSpan = nullptr;
  PerSpanData* mCurrentSpan = nullptr;

  // The container size to use when converting between logical and
  // physical coordinates for frames in this span. For the root span
  // this is the size of the block cached in mContainerSize; for
  // child spans it's the size of the root span.
  nsSize ContainerSizeForSpan(PerSpanData* aPSD) {
    return (aPSD == mRootSpan)
               ? mContainerSize
               : aPSD->mFrame->mBounds.Size(mRootSpan->mWritingMode)
                     .GetPhysicalSize(mRootSpan->mWritingMode);
  }

  // Get the advance of any trailing hangable whitespace. If the whitespace
  // has directionality opposite to the line, the result is negated.
  nscoord GetHangFrom(const PerSpanData* aSpan, bool aLineIsRTL) const;
  gfxTextRun::TrimmableWS GetTrimFrom(const PerSpanData* aSpan,
                                      bool aLineIsRTL) const;

  gfxBreakPriority mLastOptionalBreakPriority = gfxBreakPriority::eNoBreak;
  int32_t mLastOptionalBreakFrameOffset = -1;
  int32_t mForceBreakFrameOffset = -1;

  nscoord mMinLineBSize = 0;

  // The amount of text indent that we applied to this line, needed for
  // max-element-size calculation.
  nscoord mTextIndent = 0;

  // This state varies during the reflow of a line but is line
  // "global" state not span "local" state.
  int32_t mLineNumber = 0;
  mozilla::JustificationInfo mJustificationInfo;

  int32_t mTotalPlacedFrames = 0;

  nscoord mBStartEdge = 0;
  nscoord mMaxStartBoxBSize = 0;
  nscoord mMaxEndBoxBSize = 0;

  nscoord mInflationMinFontSize;

  // Final computed line-bSize value after VerticalAlignFrames for
  // the block has been called.
  nscoord mFinalLineBSize = 0;

  // The amount that the current line would be trimmed by, whether
  // or not the trim was actually applied to this line.
  nscoord mPotentialTextBoxTrimEndAmount = 0;

  // Amount of trimmable whitespace inline size for the trailing text
  // frame, if any
  nscoord mTrimmableISize = 0;

  // Physical size. Use only for physical <-> logical coordinate conversion.
  nsSize mContainerSize;
  const nsSize& ContainerSize() const { return mContainerSize; }

  bool mFirstLetterStyleOK : 1;
  bool mIsTopOfPage : 1;
  bool mImpactedByFloats : 1;
  bool mLastFloatWasLetterFrame : 1;
  bool mLineIsEmpty : 1;
  bool mLineEndsInBR : 1;
  bool mNeedBackup : 1;
  bool mInFirstLine : 1;
  bool mGotLineBox : 1;
  bool mInFirstLetter : 1;
  bool mHasMarker : 1;
  bool mDirtyNextLine : 1;
  bool mLineAtStart : 1;
  bool mHasRuby : 1;
  bool mSuppressLineWrap : 1;
  bool mUsedOverflowWrap : 1;

  int32_t mSpanDepth = 0;
#ifdef DEBUG
  int32_t mSpansAllocated = 0, mSpansFreed = 0;
  int32_t mFramesAllocated = 0, mFramesFreed = 0;
#endif

  /**
   * Per span and per frame data.
   */
  mozilla::ArenaAllocator<1024, sizeof(void*)> mArena;

  /**
   * Allocate a PerFrameData from the mArena pool. The allocation is infallible.
   */
  PerFrameData* NewPerFrameData(nsIFrame* aFrame);

  /**
   * Allocate a PerSpanData from the mArena pool. The allocation is infallible.
   */
  PerSpanData* NewPerSpanData();

  PerFrameData* LastFrame() const { return mCurrentSpan->mLastFrame; }

  /**
   * Unlink the given PerFrameData and all the siblings after it from
   * the span. The unlinked PFDs are usually freed immediately.
   * However, if PFD_ISLINKEDTOBASE is set, it won't be freed until
   * the frame of its base is unlinked.
   */
  void UnlinkFrame(PerFrameData* pfd);

  /**
   * Free the given PerFrameData.
   */
  void FreeFrame(PerFrameData* pfd);

  void FreeSpan(PerSpanData* psd);

  bool InBlockContext() const { return mSpanDepth == 0; }

  void PushFrame(nsIFrame* aFrame);

  void AllowForStartMargin(PerFrameData* pfd, ReflowInput& aReflowInput);

  void SyncAnnotationBounds(PerFrameData* aRubyFrame);

  bool CanPlaceFrame(PerFrameData* pfd, bool aNotSafeToBreak,
                     bool aFrameCanContinueTextRun,
                     bool aCanRollBackBeforeFrame, ReflowOutput& aMetrics,
                     nsReflowStatus& aStatus, bool* aOptionalBreakAfterFits);

  void PlaceFrame(PerFrameData* pfd, ReflowOutput& aMetrics);

  void AdjustLeadings(nsIFrame* spanFrame, PerSpanData* psd,
                      const nsStyleText* aStyleText, float aInflation,
                      bool* aZeroEffectiveSpanBox);

  static void SetSpanForEmptyLine(PerSpanData* aPerSpanData,
                                  mozilla::WritingMode aWM,
                                  const nsSize& aContainerSize,
                                  nscoord aBStartEdge);
  void VerticalAlignFrames(PerSpanData* psd);

  void ApplyBlockTextBoxTrim(PerSpanData* psd, mozilla::WritingMode aLineWM,
                             nscoord* aLineBSize, nscoord* aBaselineBCoord,
                             nsFlowAreaRect* aFlowArea,
                             bool aIsLastFormattedLine);

  nscoord ComputeTopAlignFrameStart(const PerFrameData* pfd,
                                    const mozilla::WritingMode& aWM,
                                    nscoord aDistanceFromStart,
                                    nscoord aLineBSize);

  nscoord ComputeBottomAlignFrameStart(const PerFrameData* pfd,
                                       const mozilla::WritingMode& aWM,
                                       nscoord aDistanceFromStart,
                                       nscoord aLineBSize);

  void PlaceTopBottomCenterFrames(PerSpanData* psd, nscoord aDistanceFromStart,
                                  nscoord aLineBSize);

  void ApplyRelativePositioning(PerFrameData* aPFD);

  void RelativePositionAnnotations(PerSpanData* aRubyPSD,
                                   mozilla::OverflowAreas& aOverflowAreas);

  void RelativePositionFrames(PerSpanData* psd,
                              mozilla::OverflowAreas& aOverflowAreas);

  bool TrimTrailingWhiteSpaceIn(PerSpanData* psd, nscoord* aDeltaISize);

  struct JustificationComputationState;

  static int AssignInterframeJustificationGaps(
      PerFrameData* aFrame, JustificationComputationState& aState);

  int32_t ComputeFrameJustification(PerSpanData* psd,
                                    JustificationComputationState& aState);

  void AdvanceAnnotationInlineBounds(PerFrameData* aPFD,
                                     const nsSize& aContainerSize,
                                     nscoord aDeltaICoord, nscoord aDeltaISize);

  void ApplyLineJustificationToAnnotations(PerFrameData* aPFD,
                                           nscoord aDeltaICoord,
                                           nscoord aDeltaISize);

  // Apply justification.  The return value is the amount by which the width of
  // the span corresponding to aPSD got increased due to justification.
  nscoord ApplyFrameJustification(
      PerSpanData* aPSD, mozilla::JustificationApplicationState& aState);

  void ExpandRubyBox(PerFrameData* aFrame, nscoord aReservedISize,
                     const nsSize& aContainerSize);

  void ExpandRubyBoxWithAnnotations(PerFrameData* aFrame,
                                    const nsSize& aContainerSize);

  void ExpandInlineRubyBoxes(PerSpanData* aSpan);

  void AttachFrameToBaseLineLayout(PerFrameData* aFrame);

#ifdef DEBUG
  void DumpPerSpanData(PerSpanData* psd, int32_t aIndent);
#endif

 private:
  static bool ShouldApplyLineHeightInPreserveWhiteSpace(const PerSpanData* psd);
};

#endif /* nsLineLayout_h_ */
