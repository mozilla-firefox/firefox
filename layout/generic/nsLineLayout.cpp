/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* state and methods used while laying out a single line of a block frame */

#include "nsLineLayout.h"

#include <algorithm>

#include "LayoutLogging.h"
#include "RubyUtils.h"
#include "mozilla/AbsoluteContainingBlock.h"
#include "mozilla/ComputedStyle.h"
#include "mozilla/SVGTextFrame.h"
#include "mozilla/StaticPrefs_layout.h"
#include "nsBidiPresUtils.h"
#include "nsBlockFrame.h"
#include "nsContainerFrame.h"
#include "nsFloatManager.h"
#include "nsFontMetrics.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsInlineFrame.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsRubyFrame.h"
#include "nsRubyTextFrame.h"
#include "nsStyleConsts.h"
#include "nsStyleStructInlines.h"
#include "nsTextFrame.h"

#ifdef DEBUG
#  undef NOISY_INLINEDIR_ALIGN
#  undef NOISY_BLOCKDIR_ALIGN
#  undef NOISY_REFLOW
#  undef REALLY_NOISY_REFLOW
#  undef NOISY_PUSHING
#  undef REALLY_NOISY_PUSHING
#  undef NOISY_CAN_PLACE_FRAME
#  undef NOISY_TRIM
#  undef REALLY_NOISY_TRIM
#endif

using namespace mozilla;

//----------------------------------------------------------------------

nsLineLayout::nsLineLayout(nsPresContext* aPresContext,
                           nsFloatManager* aFloatManager,
                           const ReflowInput& aLineContainerRI,
                           const nsLineList::iterator* aLine,
                           nsLineLayout* aBaseLineLayout)
    : mPresContext(aPresContext),
      mFloatManager(aFloatManager),
      mLineContainerRI(aLineContainerRI),
      mBaseLineLayout(aBaseLineLayout),
      mFirstLetterStyleOK(false),
      mIsTopOfPage(false),
      mImpactedByFloats(false),
      mLastFloatWasLetterFrame(false),
      mLineIsEmpty(false),
      mLineEndsInBR(false),
      mNeedBackup(false),
      mInFirstLine(false),
      mGotLineBox(false),
      mInFirstLetter(false),
      mHasMarker(false),
      mDirtyNextLine(false),
      mLineAtStart(false),
      mHasRuby(false),
      mSuppressLineWrap(LineContainerFrame()->IsInSVGTextSubtree()),
      mUsedOverflowWrap(false) {
  NS_ASSERTION(aFloatManager || LineContainerFrame()->IsLetterFrame(),
               "float manager should be present");
  MOZ_ASSERT(
      !!mBaseLineLayout == LineContainerFrame()->IsRubyTextContainerFrame(),
      "Only ruby text container frames have a different base line layout");
  MOZ_COUNT_CTOR(nsLineLayout);

  // Stash away some style data that we need
  nsBlockFrame* blockFrame = do_QueryFrame(LineContainerFrame());
  mStyleText = blockFrame ? blockFrame->StyleTextForLineLayout()
                          : LineContainerFrame()->StyleText();

  mInflationMinFontSize =
      nsLayoutUtils::InflationMinFontSizeFor(LineContainerFrame());

  if (aLine) {
    mGotLineBox = true;
    mLineBox = *aLine;
  }
}

static bool ShouldApplyTextIndent(nsIFrame* aLineContainer) {
  if (aLineContainer->IsRubyTextContainerFrame()) {
    return false;
  }
  if (nsBlockFrame* block = do_QueryFrame(aLineContainer);
      block && block->IsTextInput()) {
    // text-indent applies to the inner text input frames individually.
    return false;
  }
  return true;
}

void nsLineLayout::BeginLineReflow(nscoord aICoord, nscoord aBCoord,
                                   nscoord aISize, nscoord aBSize,
                                   bool aImpactedByFloats, bool aIsTopOfPage,
                                   WritingMode aWritingMode,
                                   const nsSize& aContainerSize,
                                   nscoord aInset) {
  MOZ_ASSERT(nullptr == mRootSpan, "bad linelayout user");
  LAYOUT_WARN_IF_FALSE(aISize != NS_UNCONSTRAINEDSIZE,
                       "have unconstrained width; this should only result from "
                       "very large sizes, not attempts at intrinsic width "
                       "calculation");
#ifdef DEBUG
  if ((aISize != NS_UNCONSTRAINEDSIZE) && ABSURD_SIZE(aISize) &&
      !LineContainerFrame()->GetParent()->IsAbsurdSizeAssertSuppressed()) {
    LineContainerFrame()->ListTag(stdout);
    printf(": Init: bad caller: width WAS %d(0x%x)\n", aISize, aISize);
  }
  if ((aBSize != NS_UNCONSTRAINEDSIZE) && ABSURD_SIZE(aBSize) &&
      !LineContainerFrame()->GetParent()->IsAbsurdSizeAssertSuppressed()) {
    LineContainerFrame()->ListTag(stdout);
    printf(": Init: bad caller: height WAS %d(0x%x)\n", aBSize, aBSize);
  }
#endif
#ifdef NOISY_REFLOW
  LineContainerFrame()->ListTag(stdout);
  printf(": BeginLineReflow: %d,%d,%d,%d impacted=%s %s\n", aICoord, aBCoord,
         aISize, aBSize, aImpactedByFloats ? "true" : "false",
         aIsTopOfPage ? "top-of-page" : "");
#endif
#ifdef DEBUG
  mSpansAllocated = mSpansFreed = mFramesAllocated = mFramesFreed = 0;
#endif

  mFirstLetterStyleOK = false;
  mIsTopOfPage = aIsTopOfPage;
  mImpactedByFloats = aImpactedByFloats;
  mTotalPlacedFrames = 0;
  if (!mBaseLineLayout) {
    mLineIsEmpty = true;
    mLineAtStart = true;
  } else {
    mLineIsEmpty = false;
    mLineAtStart = false;
  }
  mLineEndsInBR = false;
  mSpanDepth = 0;
  mMaxStartBoxBSize = mMaxEndBoxBSize = 0;

  if (mGotLineBox) {
    mLineBox->ClearHasMarker();
  }

  PerSpanData* psd = NewPerSpanData();
  mCurrentSpan = mRootSpan = psd;
  psd->mReflowInput = &mLineContainerRI;
  psd->mIStart = aICoord;
  psd->mICoord = aICoord;
  psd->mIEnd = aICoord + aISize;
  // Set up inset to be used for text-wrap:balance implementation, but only if
  // the available size is greater than inset.
  psd->mInset = aISize > aInset ? aInset : 0;
  mContainerSize = aContainerSize;

  mBStartEdge = aBCoord;

  psd->mNoWrap = !mStyleText->WhiteSpaceCanWrapStyle() || mSuppressLineWrap;
  psd->mWritingMode = aWritingMode;

  // Determine if this is the first line of the block (or first after a hard
  // line-break, if `each-line` is in effect).
  nsIFrame* containerFrame = LineContainerFrame();
  if (!mStyleText->mTextIndent.length.IsDefinitelyZero() &&
      ShouldApplyTextIndent(containerFrame)) {
    bool isFirstLineOrAfterHardBreak = [&] {
      if (mLineNumber > 0) {
        return mStyleText->mTextIndent.each_line && GetLine() &&
               !GetLine()->prev()->IsLineWrapped();
      }
      if (nsBlockFrame* prevBlock =
              do_QueryFrame(containerFrame->GetPrevInFlow())) {
        return mStyleText->mTextIndent.each_line &&
               (prevBlock->Lines().empty() ||
                !prevBlock->LinesEnd().prev()->IsLineWrapped());
      }
      return true;
    }();

    // Resolve and apply the text-indent value if this line requires it.
    // The `hanging` option inverts which lines are to be indented.
    if (isFirstLineOrAfterHardBreak != mStyleText->mTextIndent.hanging) {
      nscoord pctBasis = mLineContainerRI.ComputedISize();
      mTextIndent = mStyleText->mTextIndent.length.Resolve(pctBasis);
      psd->mICoord += mTextIndent;
    }
  }

  PerFrameData* pfd = NewPerFrameData(containerFrame);
  pfd->mAscent = 0;
  pfd->mSpan = psd;
  psd->mFrame = pfd;
  if (containerFrame->IsRubyTextContainerFrame()) {
    // Ruby text container won't be reflowed via ReflowFrame, hence the
    // relative positioning information should be recorded here.
    MOZ_ASSERT(mBaseLineLayout != this);
    pfd->mIsRelativelyOrStickyPos =
        mLineContainerRI.mStyleDisplay->IsRelativelyOrStickyPositionedStyle();
    if (pfd->mIsRelativelyOrStickyPos) {
      MOZ_ASSERT(mLineContainerRI.GetWritingMode() == pfd->mWritingMode,
                 "mLineContainerRI.frame == frame, "
                 "hence they should have identical writing mode");
      pfd->mOffsets =
          mLineContainerRI.ComputedLogicalOffsets(pfd->mWritingMode);
    }
  }
}

bool nsLineLayout::EndLineReflow() {
#ifdef NOISY_REFLOW
  LineContainerFrame()->ListTag(stdout);
  printf(": EndLineReflow: width=%d\n",
         mRootSpan->mICoord - mRootSpan->mIStart);
#endif

  NS_ASSERTION(!mBaseLineLayout ||
                   (!mSpansAllocated && !mSpansFreed && !mSpanFreeList &&
                    !mFramesAllocated && !mFramesFreed && !mFrameFreeList),
               "Allocated frames or spans on non-base line layout?");
  MOZ_ASSERT(mRootSpan == mCurrentSpan);

  UnlinkFrame(mRootSpan->mFrame);
  mCurrentSpan = mRootSpan = nullptr;

  NS_ASSERTION(mSpansAllocated == mSpansFreed, "leak");
  NS_ASSERTION(mFramesAllocated == mFramesFreed, "leak");

#if 0
  static int32_t maxSpansAllocated = NS_LINELAYOUT_NUM_SPANS;
  static int32_t maxFramesAllocated = NS_LINELAYOUT_NUM_FRAMES;
  if (mSpansAllocated > maxSpansAllocated) {
    printf("XXX: saw a line with %d spans\n", mSpansAllocated);
    maxSpansAllocated = mSpansAllocated;
  }
  if (mFramesAllocated > maxFramesAllocated) {
    printf("XXX: saw a line with %d frames\n", mFramesAllocated);
    maxFramesAllocated = mFramesAllocated;
  }
#endif

  return mUsedOverflowWrap;
}

// XXX swtich to a single mAvailLineWidth that we adjust as each frame
// on the line is placed. Each span can still have a per-span mICoord that
// tracks where a child frame is going in its span; they don't need a
// per-span mIStart?

void nsLineLayout::UpdateBand(WritingMode aWM,
                              const LogicalRect& aNewAvailSpace,
                              nsIFrame* aFloatFrame) {
  WritingMode lineWM = mRootSpan->mWritingMode;
  // need to convert to our writing mode, because we might have a different
  // mode from the caller due to dir: auto
  LogicalRect availSpace =
      aNewAvailSpace.ConvertTo(lineWM, aWM, ContainerSize());
#ifdef REALLY_NOISY_REFLOW
  printf(
      "nsLL::UpdateBand %d, %d, %d, %d, (converted to %d, %d, %d, %d); "
      "frame=%p\n  will set mImpacted to true\n",
      aNewAvailSpace.IStart(aWM), aNewAvailSpace.BStart(aWM),
      aNewAvailSpace.ISize(aWM), aNewAvailSpace.BSize(aWM),
      availSpace.IStart(lineWM), availSpace.BStart(lineWM),
      availSpace.ISize(lineWM), availSpace.BSize(lineWM), aFloatFrame);
#endif
#ifdef DEBUG
  if ((availSpace.ISize(lineWM) != NS_UNCONSTRAINEDSIZE) &&
      ABSURD_SIZE(availSpace.ISize(lineWM)) &&
      !LineContainerFrame()->GetParent()->IsAbsurdSizeAssertSuppressed()) {
    LineContainerFrame()->ListTag(stdout);
    printf(": UpdateBand: bad caller: ISize WAS %d(0x%x)\n",
           availSpace.ISize(lineWM), availSpace.ISize(lineWM));
  }
  if ((availSpace.BSize(lineWM) != NS_UNCONSTRAINEDSIZE) &&
      ABSURD_SIZE(availSpace.BSize(lineWM)) &&
      !LineContainerFrame()->GetParent()->IsAbsurdSizeAssertSuppressed()) {
    LineContainerFrame()->ListTag(stdout);
    printf(": UpdateBand: bad caller: BSize WAS %d(0x%x)\n",
           availSpace.BSize(lineWM), availSpace.BSize(lineWM));
  }
#endif

  // Compute the difference between last times width and the new width
  NS_WARNING_ASSERTION(
      mRootSpan->mIEnd != NS_UNCONSTRAINEDSIZE &&
          availSpace.ISize(lineWM) != NS_UNCONSTRAINEDSIZE,
      "have unconstrained inline size; this should only result from very large "
      "sizes, not attempts at intrinsic width calculation");
  // The root span's mIStart moves to aICoord
  nscoord deltaICoord = availSpace.IStart(lineWM) - mRootSpan->mIStart;
  // The inline size of all spans changes by this much (the root span's
  // mIEnd moves to aICoord + aISize, its new inline size is aISize)
  nscoord deltaISize =
      availSpace.ISize(lineWM) - (mRootSpan->mIEnd - mRootSpan->mIStart);
#ifdef NOISY_REFLOW
  LineContainerFrame()->ListTag(stdout);
  printf(": UpdateBand: %d,%d,%d,%d deltaISize=%d deltaICoord=%d\n",
         availSpace.IStart(lineWM), availSpace.BStart(lineWM),
         availSpace.ISize(lineWM), availSpace.BSize(lineWM), deltaISize,
         deltaICoord);
#endif

  // Update the root span position
  mRootSpan->mIStart += deltaICoord;
  mRootSpan->mIEnd += deltaICoord;
  mRootSpan->mICoord += deltaICoord;

  // Now update the right edges of the open spans to account for any
  // change in available space width
  for (PerSpanData* psd = mCurrentSpan; psd; psd = psd->mParent) {
    psd->mIEnd += deltaISize;
    psd->mContainsFloat = true;
#ifdef NOISY_REFLOW
    printf("  span %p: oldIEnd=%d newIEnd=%d\n", psd, psd->mIEnd - deltaISize,
           psd->mIEnd);
#endif
  }
  NS_ASSERTION(mRootSpan->mContainsFloat &&
                   mRootSpan->mIStart == availSpace.IStart(lineWM) &&
                   mRootSpan->mIEnd == availSpace.IEnd(lineWM),
               "root span was updated incorrectly?");

  // Update frame bounds
  // Note: Only adjust the outermost frames (the ones that are direct
  // children of the block), not the ones in the child spans. The reason
  // is simple: the frames in the spans have coordinates local to their
  // parent therefore they are moved when their parent span is moved.
  if (deltaICoord != 0) {
    for (PerFrameData* pfd = mRootSpan->mFirstFrame; pfd; pfd = pfd->mNext) {
      pfd->mBounds.IStart(lineWM) += deltaICoord;
    }
  }

  mBStartEdge = availSpace.BStart(lineWM);
  mImpactedByFloats = true;

  mLastFloatWasLetterFrame = aFloatFrame->IsLetterFrame();
}

nsLineLayout::PerSpanData* nsLineLayout::NewPerSpanData() {
  nsLineLayout* outerLineLayout = GetOutermostLineLayout();
  PerSpanData* psd = outerLineLayout->mSpanFreeList;
  if (!psd) {
    void* mem = outerLineLayout->mArena.Allocate(sizeof(PerSpanData));
    psd = reinterpret_cast<PerSpanData*>(mem);
  } else {
    outerLineLayout->mSpanFreeList = psd->mNextFreeSpan;
  }
  psd->mParent = nullptr;
  psd->mFrame = nullptr;
  psd->mFirstFrame = nullptr;
  psd->mLastFrame = nullptr;
  psd->mReflowInput = nullptr;
  psd->mContainsFloat = false;
  psd->mHasNonemptyContent = false;
  psd->mBaseline = nullptr;

#ifdef DEBUG
  outerLineLayout->mSpansAllocated++;
#endif
  return psd;
}

void nsLineLayout::BeginSpan(nsIFrame* aFrame,
                             const ReflowInput* aSpanReflowInput,
                             nscoord aIStart, nscoord aIEnd,
                             nscoord* aBaseline) {
  NS_ASSERTION(aIEnd != NS_UNCONSTRAINEDSIZE,
               "should no longer be using unconstrained sizes");
#ifdef NOISY_REFLOW
  nsIFrame::IndentBy(stdout, mSpanDepth + 1);
  aFrame->ListTag(stdout);
  printf(": BeginSpan leftEdge=%d rightEdge=%d\n", aIStart, aIEnd);
#endif

  PerSpanData* psd = NewPerSpanData();
  // Link up span frame's pfd to point to its child span data
  PerFrameData* pfd = mCurrentSpan->mLastFrame;
  NS_ASSERTION(pfd->mFrame == aFrame, "huh?");
  pfd->mSpan = psd;

  // Init new span
  psd->mFrame = pfd;
  psd->mParent = mCurrentSpan;
  psd->mReflowInput = aSpanReflowInput;
  psd->mIStart = aIStart;
  psd->mICoord = aIStart;
  psd->mIEnd = aIEnd;
  psd->mInset = 0;  // inset applies only to the root span
  psd->mBaseline = aBaseline;

  nsIFrame* frame = aSpanReflowInput->mFrame;
  psd->mNoWrap = !frame->StyleText()->WhiteSpaceCanWrap(frame) ||
                 mSuppressLineWrap || frame->Style()->ShouldSuppressLineBreak();
  psd->mWritingMode = aSpanReflowInput->GetWritingMode();

  // Switch to new span
  mCurrentSpan = psd;
  mSpanDepth++;
}

nscoord nsLineLayout::EndSpan(nsIFrame* aFrame) {
  NS_ASSERTION(mSpanDepth > 0, "end-span without begin-span");
#ifdef NOISY_REFLOW
  nsIFrame::IndentBy(stdout, mSpanDepth);
  aFrame->ListTag(stdout);
  printf(": EndSpan width=%d\n", mCurrentSpan->mICoord - mCurrentSpan->mIStart);
#endif
  PerSpanData* psd = mCurrentSpan;
  MOZ_ASSERT(psd->mParent, "We never call this on the root");

  if (psd->mNoWrap && !psd->mParent->mNoWrap) {
    FlushNoWrapFloats();
  }

  nscoord iSizeResult = psd->mLastFrame ? (psd->mICoord - psd->mIStart) : 0;

  mSpanDepth--;
  mCurrentSpan->mReflowInput = nullptr;  // no longer valid so null it out!
  mCurrentSpan = mCurrentSpan->mParent;
  return iSizeResult;
}

void nsLineLayout::AttachFrameToBaseLineLayout(PerFrameData* aFrame) {
  MOZ_ASSERT(mBaseLineLayout,
             "This method must not be called in a base line layout.");

  PerFrameData* baseFrame = mBaseLineLayout->LastFrame();
  MOZ_ASSERT(aFrame && baseFrame);
  MOZ_ASSERT(!aFrame->mIsLinkedToBase,
             "The frame must not have been linked with the base");
#ifdef DEBUG
  LayoutFrameType baseType = baseFrame->mFrame->Type();
  LayoutFrameType annotationType = aFrame->mFrame->Type();
  MOZ_ASSERT((baseType == LayoutFrameType::RubyBaseContainer &&
              annotationType == LayoutFrameType::RubyTextContainer) ||
             (baseType == LayoutFrameType::RubyBase &&
              annotationType == LayoutFrameType::RubyText));
#endif

  aFrame->mNextAnnotation = baseFrame->mNextAnnotation;
  baseFrame->mNextAnnotation = aFrame;
  aFrame->mIsLinkedToBase = true;
}

int32_t nsLineLayout::GetCurrentSpanCount() const {
  MOZ_ASSERT(mCurrentSpan == mRootSpan, "bad linelayout user");
  int32_t count = 0;
  for (const auto* pfd = mRootSpan->mFirstFrame; pfd; pfd = pfd->mNext) {
    count++;
  }
  return count;
}

void nsLineLayout::SplitLineTo(int32_t aNewCount) {
  MOZ_ASSERT(mCurrentSpan == mRootSpan, "bad linelayout user");

#ifdef REALLY_NOISY_PUSHING
  printf("SplitLineTo %d (current count=%d); before:\n", aNewCount,
         GetCurrentSpanCount());
  DumpPerSpanData(mRootSpan, 1);
#endif
  for (auto* pfd = mRootSpan->mFirstFrame; pfd; pfd = pfd->mNext) {
    if (--aNewCount == 0) {
      // Truncate list at pfd (we keep pfd, but anything following is freed)
      PerFrameData* next = pfd->mNext;
      pfd->mNext = nullptr;
      mRootSpan->mLastFrame = pfd;

      // Now unlink all of the frames following pfd
      UnlinkFrame(next);
      break;
    }
  }
#ifdef NOISY_PUSHING
  printf("SplitLineTo %d (current count=%d); after:\n", aNewCount,
         GetCurrentSpanCount());
  DumpPerSpanData(mRootSpan, 1);
#endif
}

void nsLineLayout::PushFrame(nsIFrame* aFrame) {
  PerSpanData* psd = mCurrentSpan;
  NS_ASSERTION(psd->mLastFrame->mFrame == aFrame, "pushing non-last frame");

#ifdef REALLY_NOISY_PUSHING
  nsIFrame::IndentBy(stdout, mSpanDepth);
  printf("PushFrame %p, before:\n", psd);
  DumpPerSpanData(psd, 1);
#endif

  // Take the last frame off of the span's frame list
  PerFrameData* pfd = psd->mLastFrame;
  if (pfd == psd->mFirstFrame) {
    // We are pushing away the only frame...empty the list
    psd->mFirstFrame = nullptr;
    psd->mLastFrame = nullptr;
  } else {
    PerFrameData* prevFrame = pfd->mPrev;
    prevFrame->mNext = nullptr;
    psd->mLastFrame = prevFrame;
  }

  // Now unlink the frame
  MOZ_ASSERT(!pfd->mNext);
  UnlinkFrame(pfd);
#ifdef NOISY_PUSHING
  nsIFrame::IndentBy(stdout, mSpanDepth);
  printf("PushFrame: %p after:\n", psd);
  DumpPerSpanData(psd, 1);
#endif
}

void nsLineLayout::UnlinkFrame(PerFrameData* pfd) {
  while (nullptr != pfd) {
    PerFrameData* next = pfd->mNext;
    if (pfd->mIsLinkedToBase) {
      // This frame is linked to a ruby base, and should not be freed
      // now. Just unlink it from the span. It will be freed when its
      // base frame gets unlinked.
      pfd->mNext = pfd->mPrev = nullptr;
      pfd = next;
      continue;
    }

    // It is a ruby base frame. If there are any annotations
    // linked to this frame, free them first.
    PerFrameData* annotationPFD = pfd->mNextAnnotation;
    while (annotationPFD) {
      PerFrameData* nextAnnotation = annotationPFD->mNextAnnotation;
      MOZ_ASSERT(
          annotationPFD->mNext == nullptr && annotationPFD->mPrev == nullptr,
          "PFD in annotations should have been unlinked.");
      FreeFrame(annotationPFD);
      annotationPFD = nextAnnotation;
    }

    FreeFrame(pfd);
    pfd = next;
  }
}

void nsLineLayout::FreeFrame(PerFrameData* pfd) {
  if (nullptr != pfd->mSpan) {
    FreeSpan(pfd->mSpan);
  }
  nsLineLayout* outerLineLayout = GetOutermostLineLayout();
  pfd->mNext = outerLineLayout->mFrameFreeList;
  outerLineLayout->mFrameFreeList = pfd;
#ifdef DEBUG
  outerLineLayout->mFramesFreed++;
#endif
}

void nsLineLayout::FreeSpan(PerSpanData* psd) {
  // Unlink its frames
  UnlinkFrame(psd->mFirstFrame);

  nsLineLayout* outerLineLayout = GetOutermostLineLayout();
  // Now put the span on the free list since it's free too
  psd->mNextFreeSpan = outerLineLayout->mSpanFreeList;
  outerLineLayout->mSpanFreeList = psd;
#ifdef DEBUG
  outerLineLayout->mSpansFreed++;
#endif
}

bool nsLineLayout::IsZeroBSize() const {
  for (const auto* pfd = mCurrentSpan->mFirstFrame; pfd; pfd = pfd->mNext) {
    if (0 != pfd->mBounds.BSize(mCurrentSpan->mWritingMode)) {
      return false;
    }
  }
  return true;
}

nsLineLayout::PerFrameData* nsLineLayout::NewPerFrameData(nsIFrame* aFrame) {
  nsLineLayout* outerLineLayout = GetOutermostLineLayout();
  PerFrameData* pfd = outerLineLayout->mFrameFreeList;
  if (!pfd) {
    void* mem = outerLineLayout->mArena.Allocate(sizeof(PerFrameData));
    pfd = reinterpret_cast<PerFrameData*>(mem);
  } else {
    outerLineLayout->mFrameFreeList = pfd->mNext;
  }
  pfd->mSpan = nullptr;
  pfd->mNext = nullptr;
  pfd->mPrev = nullptr;
  pfd->mNextAnnotation = nullptr;
  pfd->mFrame = aFrame;

  // all flags default to false
  pfd->mIsRelativelyOrStickyPos = false;
  pfd->mIsTextFrame = false;
  pfd->mIsNonEmptyTextFrame = false;
  pfd->mIsNonWhitespaceTextFrame = false;
  pfd->mIsLetterFrame = false;
  pfd->mRecomputeOverflow = false;
  pfd->mIsMarker = false;
  pfd->mSkipWhenTrimmingWhitespace = false;
  pfd->mIsEmpty = false;
  pfd->mIsPlaceholder = false;
  pfd->mIsLinkedToBase = false;

  pfd->mWritingMode = aFrame->GetWritingMode();
  WritingMode lineWM = mRootSpan->mWritingMode;
  pfd->mBounds = LogicalRect(lineWM);
  pfd->mOverflowAreas.Clear();
  pfd->mMargin = LogicalMargin(lineWM);
  pfd->mBorderPadding = LogicalMargin(lineWM);
  pfd->mOffsets = LogicalMargin(pfd->mWritingMode);

  pfd->mJustificationInfo = JustificationInfo();
  pfd->mJustificationAssignment = JustificationAssignment();

#ifdef DEBUG
  pfd->mBlockDirAlign = 0xFF;
  outerLineLayout->mFramesAllocated++;
#endif
  return pfd;
}

// Checks all four sides for percentage units.  This means it should
// only be used for things (margin, padding) where percentages on top
// and bottom depend on the *width* just like percentages on left and
// right.
template <typename T>
static bool HasPercentageUnitSide(const StyleRect<T>& aSides) {
  return aSides.Any([](const auto& aLength) { return aLength.HasPercent(); });
}

static bool HasPercentageUnitMargin(const nsStyleMargin& aStyleMargin,
                                    const AnchorPosResolutionParams& aParams) {
  for (const auto side : AllPhysicalSides()) {
    if (aStyleMargin.GetMargin(side, aParams)->HasPercent()) {
      return true;
    }
  }
  return false;
}

static bool IsPercentageAware(const nsIFrame* aFrame, WritingMode aWM) {
  MOZ_ASSERT(aFrame, "null frame is not allowed");

  LayoutFrameType fType = aFrame->Type();
  if (fType == LayoutFrameType::Text) {
    // None of these things can ever be true for text frames.
    return false;
  }

  // Some of these things don't apply to non-replaced inline frames
  // (that is, fType == LayoutFrameType::Inline), but we won't bother making
  // things unnecessarily complicated, since they'll probably be set
  // quite rarely.

  const nsStyleMargin* margin = aFrame->StyleMargin();
  const auto anchorResolutionParams = AnchorPosResolutionParams::From(aFrame);
  if (HasPercentageUnitMargin(*margin, anchorResolutionParams)) {
    return true;
  }

  const nsStylePadding* padding = aFrame->StylePadding();
  if (HasPercentageUnitSide(padding->mPadding)) {
    return true;
  }

  // Note that borders can't be aware of percentages

  const nsStylePosition* pos = aFrame->StylePosition();
  const auto iSize = pos->ISize(aWM, anchorResolutionParams);
  const auto anchorOffsetResolutionParams =
      AnchorPosOffsetResolutionParams::UseCBFrameSize(anchorResolutionParams);
  if ((nsStylePosition::ISizeDependsOnContainer(iSize) && !iSize->IsAuto()) ||
      nsStylePosition::MaxISizeDependsOnContainer(
          pos->MaxISize(aWM, anchorResolutionParams)) ||
      nsStylePosition::MinISizeDependsOnContainer(
          pos->MinISize(aWM, anchorResolutionParams)) ||
      pos->GetAnchorResolvedInset(LogicalSide::IStart, aWM,
                                  anchorOffsetResolutionParams)
          ->HasPercent() ||
      pos->GetAnchorResolvedInset(LogicalSide::IEnd, aWM,
                                  anchorOffsetResolutionParams)
          ->HasPercent()) {
    return true;
  }

  if (iSize->IsAuto()) {
    // We need to check for frames that shrink-wrap when they're auto
    // width.
    const nsStyleDisplay* disp = aFrame->StyleDisplay();
    if ((disp->DisplayOutside() == StyleDisplayOutside::Inline &&
         (disp->DisplayInside() == StyleDisplayInside::FlowRoot ||
          disp->DisplayInside() == StyleDisplayInside::Table)) ||
        fType == LayoutFrameType::FieldSet) {
      return true;
    }

    // Per CSS 2.1, section 10.3.2:
    //   If 'height' and 'width' both have computed values of 'auto' and
    //   the element has an intrinsic ratio but no intrinsic height or
    //   width and the containing block's width does not itself depend
    //   on the replaced element's width, then the used value of 'width'
    //   is calculated from the constraint equation used for
    //   block-level, non-replaced elements in normal flow.
    nsIFrame* f = const_cast<nsIFrame*>(aFrame);
    if (f->GetAspectRatio() &&
        // Some percents are treated like 'auto', so check != coord
        !pos->BSize(aWM, anchorResolutionParams)->ConvertsToLength()) {
      const IntrinsicSize& intrinsicSize = f->GetIntrinsicSize();
      if (!intrinsicSize.width && !intrinsicSize.height) {
        return true;
      }
    }
  }

  return false;
}

void nsLineLayout::ReflowFrame(nsIFrame* aFrame, nsReflowStatus& aReflowStatus,
                               ReflowOutput* aMetrics, bool& aPushedFrame) {
  // Initialize OUT parameter
  aPushedFrame = false;

  PerFrameData* pfd = NewPerFrameData(aFrame);
  PerSpanData* psd = mCurrentSpan;
  psd->AppendFrame(pfd);

#ifdef REALLY_NOISY_REFLOW
  nsIFrame::IndentBy(stdout, mSpanDepth);
  printf("%p: Begin ReflowFrame pfd=%p ", psd, pfd);
  aFrame->ListTag(stdout);
  printf("\n");
#endif

  if (mCurrentSpan == mRootSpan) {
    pfd->mFrame->RemoveProperty(nsIFrame::LineBaselineOffset());
  } else {
#ifdef DEBUG
    bool hasLineOffset;
    pfd->mFrame->GetProperty(nsIFrame::LineBaselineOffset(), &hasLineOffset);
    NS_ASSERTION(!hasLineOffset,
                 "LineBaselineOffset was set but was not expected");
#endif
  }

  mJustificationInfo = JustificationInfo();

  // Stash copies of some of the computed state away for later
  // (block-direction alignment, for example)
  WritingMode frameWM = pfd->mWritingMode;
  WritingMode lineWM = mRootSpan->mWritingMode;

  // NOTE: While the inline direction coordinate remains relative to the
  // parent span, the block direction coordinate is fixed at the top
  // edge for the line. During VerticalAlignFrames we will repair this
  // so that the block direction coordinate is properly set and relative
  // to the appropriate span.
  pfd->mBounds.IStart(lineWM) = psd->mICoord;
  pfd->mBounds.BStart(lineWM) = mBStartEdge;

  // We want to guarantee that we always make progress when
  // formatting. Therefore, if the object being placed on the line is
  // too big for the line, but it is the only thing on the line and is not
  // impacted by a float, then we go ahead and place it anyway. (If the line
  // is impacted by one or more floats, then it is safe to break because
  // we can move the line down below float(s).)
  //
  // Capture this state *before* we reflow the frame in case it clears
  // the state out. We need to know how to treat the current frame
  // when breaking.
  bool notSafeToBreak = LineIsEmpty() && !mImpactedByFloats;

  // Figure out whether we're talking about a textframe here
  LayoutFrameType frameType = aFrame->Type();
  const bool isText = frameType == LayoutFrameType::Text;

  // Inline-ish and text-ish things don't compute their width;
  // everything else does.  We need to give them an available width that
  // reflects the space left on the line.
  LAYOUT_WARN_IF_FALSE(psd->mIEnd != NS_UNCONSTRAINEDSIZE,
                       "have unconstrained width; this should only result from "
                       "very large sizes, not attempts at intrinsic width "
                       "calculation");
  nscoord availableSpaceOnLine = psd->mIEnd - psd->mICoord - psd->mInset;

  // Setup reflow input for reflowing the frame
  Maybe<ReflowInput> reflowInputHolder;
  if (!isText) {
    // Compute the available size for the frame. This available width
    // includes room for the side margins.
    // For now, set the available block-size to unconstrained always.
    LogicalSize availSize = mLineContainerRI.ComputedSize(frameWM);
    availSize.BSize(frameWM) = NS_UNCONSTRAINEDSIZE;
    reflowInputHolder.emplace(mPresContext, *psd->mReflowInput, aFrame,
                              availSize);
    ReflowInput& reflowInput = *reflowInputHolder;
    reflowInput.mLineLayout = this;
    reflowInput.mFlags.mIsTopOfPage = mIsTopOfPage;
    if (reflowInput.ComputedISize() == NS_UNCONSTRAINEDSIZE) {
      reflowInput.SetAvailableISize(availableSpaceOnLine);
    }
    pfd->mMargin = reflowInput.ComputedLogicalMargin(lineWM);
    pfd->mBorderPadding = reflowInput.ComputedLogicalBorderPadding(lineWM);
    pfd->mIsRelativelyOrStickyPos =
        reflowInput.mStyleDisplay->IsRelativelyOrStickyPositionedStyle();
    if (pfd->mIsRelativelyOrStickyPos) {
      pfd->mOffsets = reflowInput.ComputedLogicalOffsets(frameWM);
    }

    // Calculate whether the the frame should have a start margin and
    // subtract the margin from the available width if necessary.
    // The margin will be applied to the starting inline coordinates of
    // the frame in CanPlaceFrame() after reflowing the frame.
    AllowForStartMargin(pfd, reflowInput);
  }
  // if isText(), no need to propagate NS_FRAME_IS_DIRTY from the parent,
  // because reflow doesn't look at the dirty bits on the frame being reflowed.

  // See if this frame depends on the inline-size of its containing block.
  // If so, disable resize reflow optimizations for the line.  (Note that,
  // to be conservative, we do this if we *try* to fit a frame on a
  // line, even if we don't succeed.)  (Note also that we can only make
  // this IsPercentageAware check *after* we've constructed our
  // ReflowInput, because that construction may be what forces aFrame
  // to lazily initialize its (possibly-percent-valued) intrinsic size.)
  if (mGotLineBox && IsPercentageAware(aFrame, lineWM)) {
    mLineBox->DisableResizeReflowOptimization();
  }

  // Note that we don't bother positioning the frame yet, because we're probably
  // going to end up moving it when we do the block-direction alignment.

  // Adjust float manager coordinate system for the frame.
  ReflowOutput reflowOutput(lineWM);
#ifdef DEBUG
  reflowOutput.ISize(lineWM) = nscoord(0xdeadbeef);
  reflowOutput.BSize(lineWM) = nscoord(0xdeadbeef);
#endif
  nscoord tI = pfd->mBounds.LineLeft(lineWM, ContainerSize());
  nscoord tB = pfd->mBounds.BStart(lineWM);
  mFloatManager->Translate(tI, tB);

  int32_t savedOptionalBreakOffset;
  gfxBreakPriority savedOptionalBreakPriority;
  nsIFrame* savedOptionalBreakFrame = GetLastOptionalBreakPosition(
      &savedOptionalBreakOffset, &savedOptionalBreakPriority);

  if (!isText) {
    aFrame->Reflow(mPresContext, reflowOutput, *reflowInputHolder,
                   aReflowStatus);
  } else {
    static_cast<nsTextFrame*>(aFrame)->ReflowText(
        *this, availableSpaceOnLine,
        psd->mReflowInput->mRenderingContext->GetDrawTarget(), reflowOutput,
        aReflowStatus);
  }

  pfd->mJustificationInfo = mJustificationInfo;
  mJustificationInfo = JustificationInfo();

  // See if the frame is a placeholderFrame and if it is process
  // the float. At the same time, check if the frame has any non-collapsed-away
  // content.
  bool placedFloat = false;
  bool isEmpty;
  if (frameType == LayoutFrameType::None) {
    isEmpty = pfd->mFrame->IsEmpty();
  } else if (LayoutFrameType::Placeholder == frameType) {
    isEmpty = true;
    pfd->mIsPlaceholder = true;
    pfd->mSkipWhenTrimmingWhitespace = true;
    nsIFrame* outOfFlowFrame = nsLayoutUtils::GetFloatFromPlaceholder(aFrame);
    if (outOfFlowFrame) {
      if (psd->mNoWrap &&
          // We can always place floats in an empty line.
          !LineIsEmpty() &&
          // We always place floating letter frames. This kinda sucks. They'd
          // usually fall into the LineIsEmpty() check anyway, except when
          // there's something like a ::marker before or what not. We actually
          // need to place them now, because they're pretty nasty and they
          // create continuations that are in flow and not a kid of the
          // previous continuation's parent. We don't want the deferred reflow
          // of the letter frame to kill a continuation after we've stored it
          // in the line layout data structures. See bug 1490281 to fix the
          // underlying issue. When that's fixed this check should be removed.
          !outOfFlowFrame->IsLetterFrame() &&
          !GetOutermostLineLayout()->mBlockRS->mFlags.mCanHaveOverflowMarkers) {
        // We'll do this at the next break opportunity.
        RecordNoWrapFloat(outOfFlowFrame);
      } else {
        placedFloat = TryToPlaceFloat(outOfFlowFrame);
      }
    }
  } else if (isText) {
    // Note non-empty text-frames for inline frame compatibility hackery
    pfd->mIsTextFrame = true;
    auto* textFrame = static_cast<nsTextFrame*>(pfd->mFrame);
    isEmpty = !textFrame->HasNoncollapsedCharacters();
    if (!isEmpty) {
      pfd->mIsNonEmptyTextFrame = true;
      pfd->mIsNonWhitespaceTextFrame =
          !textFrame->GetContent()->TextIsOnlyWhitespace();
    }
  } else if (LayoutFrameType::Br == frameType) {
    pfd->mSkipWhenTrimmingWhitespace = true;
    isEmpty = false;
  } else {
    if (LayoutFrameType::Letter == frameType) {
      pfd->mIsLetterFrame = true;
    }
    if (pfd->mSpan) {
      isEmpty = !pfd->mSpan->mHasNonemptyContent && pfd->mFrame->IsSelfEmpty();
    } else {
      isEmpty = pfd->mFrame->IsEmpty();
    }
  }
  pfd->mIsEmpty = isEmpty;

  mFloatManager->Translate(-tI, -tB);

  NS_ASSERTION(reflowOutput.ISize(lineWM) >= 0, "bad inline size");
  NS_ASSERTION(reflowOutput.BSize(lineWM) >= 0, "bad block size");
  if (reflowOutput.ISize(lineWM) < 0) {
    reflowOutput.ISize(lineWM) = 0;
  }
  if (reflowOutput.BSize(lineWM) < 0) {
    reflowOutput.BSize(lineWM) = 0;
  }

#ifdef DEBUG
  // Note: break-before means ignore the reflow metrics since the
  // frame will be reflowed another time.
  if (!aReflowStatus.IsInlineBreakBefore()) {
    if ((ABSURD_SIZE(reflowOutput.ISize(lineWM)) ||
         ABSURD_SIZE(reflowOutput.BSize(lineWM))) &&
        !LineContainerFrame()->GetParent()->IsAbsurdSizeAssertSuppressed()) {
      printf("nsLineLayout: ");
      aFrame->ListTag(stdout);
      printf(" metrics=%d,%d!\n", reflowOutput.Width(), reflowOutput.Height());
    }
    if ((reflowOutput.Width() == nscoord(0xdeadbeef)) ||
        (reflowOutput.Height() == nscoord(0xdeadbeef))) {
      printf("nsLineLayout: ");
      aFrame->ListTag(stdout);
      printf(" didn't set w/h %d,%d!\n", reflowOutput.Width(),
             reflowOutput.Height());
    }
  }
#endif

  // Unlike with non-inline reflow, the overflow area here does *not*
  // include the accumulation of the frame's bounds and its inline
  // descendants' bounds. Nor does it include the outline area; it's
  // just the union of the bounds of any absolute children. That is
  // added in later by nsLineLayout::ReflowInlineFrames.
  pfd->mOverflowAreas = reflowOutput.mOverflowAreas;

  pfd->mBounds.ISize(lineWM) = reflowOutput.ISize(lineWM);
  pfd->mBounds.BSize(lineWM) = reflowOutput.BSize(lineWM);

  // Size the frame, but |RelativePositionFrames| will size the view.
  aFrame->SetRect(lineWM, pfd->mBounds, ContainerSizeForSpan(psd));

  // Tell the frame that we're done reflowing it
  aFrame->DidReflow(mPresContext, isText ? nullptr : reflowInputHolder.ptr());

  if (aMetrics) {
    *aMetrics = reflowOutput;
  }

  if (!aReflowStatus.IsInlineBreakBefore()) {
    // If frame is complete and has a next-in-flow, we need to delete
    // them now. Do not do this when a break-before is signaled because
    // the frame is going to get reflowed again (and may end up wanting
    // a next-in-flow where it ends up).
    if (aReflowStatus.IsComplete()) {
      if (nsIFrame* kidNextInFlow = aFrame->GetNextInFlow()) {
        if (StaticPrefs::layout_abspos_fragment_aware_inline_cb_enabled()) {
          if (nsInlineFrame* inlineFrame = do_QueryFrame(aFrame)) {
            // Before removing aFrame's next-in-flows, pull all the absolute
            // children in those next-in-flows into the first inline
            // continuation in the current fragmentainer.
            nsIFrame* first =
                AbsoluteContainingBlock::FirstInlineContinuationInFragmentainer(
                    inlineFrame);
            if (AbsoluteContainingBlock* absCB =
                    first->GetAbsoluteContainingBlock()) {
              absCB->PrepareAbsoluteFrames(
                  static_cast<nsInlineFrame*>(first),
                  AbsoluteContainingBlock::PrepareReason::Destroy);
            }
          }
        }
        // Remove all of the childs next-in-flows. Make sure that we ask
        // the right parent to do the removal (it's possible that the
        // parent is not this because we are executing pullup code)
        FrameDestroyContext context(aFrame->PresShell());
        kidNextInFlow->GetParent()->DeleteNextInFlowChild(context,
                                                          kidNextInFlow, true);
      }
    }

    // Check whether this frame breaks up text runs. All frames break up text
    // runs (hence return false here) except for text frames and inline
    // containers.
    bool continuingTextRun = aFrame->CanContinueTextRun();

    // Clear any residual mTrimmableISize if this isn't a text frame
    if (!continuingTextRun && !pfd->mSkipWhenTrimmingWhitespace) {
      mTrimmableISize = 0;
    }

    // See if we can place the frame. If we can't fit it, then we
    // return now.
    bool optionalBreakAfterFits;
    NS_ASSERTION(isText || !reflowInputHolder->mStyleDisplay->IsFloating(
                               reflowInputHolder->mFrame),
                 "How'd we get a floated inline frame? "
                 "The frame ctor should've dealt with this.");
    if (CanPlaceFrame(pfd, notSafeToBreak, continuingTextRun,
                      savedOptionalBreakFrame != nullptr, reflowOutput,
                      aReflowStatus, &optionalBreakAfterFits)) {
      if (!isEmpty) {
        psd->mHasNonemptyContent = true;
        mLineIsEmpty = false;
        if (!pfd->mSpan) {
          // nonempty leaf content has been placed
          mLineAtStart = false;
        }
        if (LayoutFrameType::Ruby == frameType) {
          mHasRuby = true;
          SyncAnnotationBounds(pfd);
        }
      }

      // Place the frame, updating aBounds with the final size and
      // location.  Then apply the bottom+right margins (as
      // appropriate) to the frame.
      PlaceFrame(pfd, reflowOutput);
      PerSpanData* span = pfd->mSpan;
      if (span) {
        // The frame we just finished reflowing is an inline
        // container.  It needs its child frames aligned in the block direction,
        // so do most of it now.
        VerticalAlignFrames(span);
      }

      if (!continuingTextRun && !psd->mNoWrap) {
        if (!LineIsEmpty() || placedFloat) {
          // record soft break opportunity after this content that can't be
          // part of a text run. This is not a text frame so we know
          // that offset INT32_MAX means "after the content".
          if ((!aFrame->IsPlaceholderFrame() || LineIsEmpty()) &&
              NotifyOptionalBreakPosition(aFrame, INT32_MAX,
                                          optionalBreakAfterFits,
                                          gfxBreakPriority::eNormalBreak)) {
            // If this returns true then we are being told to actually break
            // here.
            aReflowStatus.SetInlineLineBreakAfter();
          }
        }
      }
    } else {
      PushFrame(aFrame);
      aPushedFrame = true;
      // Undo any saved break positions that the frame might have told us about,
      // since we didn't end up placing it
      RestoreSavedBreakPosition(savedOptionalBreakFrame,
                                savedOptionalBreakOffset,
                                savedOptionalBreakPriority);
    }
  } else {
    PushFrame(aFrame);
    aPushedFrame = true;
  }

#ifdef REALLY_NOISY_REFLOW
  nsIFrame::IndentBy(stdout, mSpanDepth);
  printf("End ReflowFrame ");
  aFrame->ListTag(stdout);
  printf(" status=%x\n", aReflowStatus);
#endif
}

void nsLineLayout::AllowForStartMargin(PerFrameData* pfd,
                                       ReflowInput& aReflowInput) {
  NS_ASSERTION(!aReflowInput.mStyleDisplay->IsFloating(aReflowInput.mFrame),
               "How'd we get a floated inline frame? "
               "The frame ctor should've dealt with this.");

  WritingMode lineWM = mRootSpan->mWritingMode;

  // Only apply start-margin on the first-in flow for inline frames,
  // and make sure to not apply it to any inline other than the first
  // in an ib split.  Note that the ib sibling (block-in-inline
  // sibling) annotations only live on the first continuation, but we
  // don't want to apply the start margin for later continuations
  // anyway.  For box-decoration-break:clone we apply the start-margin
  // on all continuations.
  if ((pfd->mFrame->GetPrevContinuation() ||
       pfd->mFrame->FrameIsNonFirstInIBSplit()) &&
      aReflowInput.mStyleBorder->mBoxDecorationBreak ==
          StyleBoxDecorationBreak::Slice) {
    // Zero this out so that when we compute the max-element-width of
    // the frame we will properly avoid adding in the starting margin.
    pfd->mMargin.IStart(lineWM) = 0;
  } else if (NS_UNCONSTRAINEDSIZE == aReflowInput.ComputedISize()) {
    NS_WARNING_ASSERTION(
        NS_UNCONSTRAINEDSIZE != aReflowInput.AvailableISize(),
        "have unconstrained inline-size; this should only result from very "
        "large sizes, not attempts at intrinsic inline-size calculation");
    // For inline-ish and text-ish things (which don't compute widths
    // in the reflow input), adjust available inline-size to account
    // for the start margin. The end margin will be accounted for when
    // we finish flowing the frame.
    WritingMode wm = aReflowInput.GetWritingMode();
    aReflowInput.SetAvailableISize(
        aReflowInput.AvailableISize() -
        pfd->mMargin.ConvertTo(wm, lineWM).IStart(wm));
  }
}

nscoord nsLineLayout::GetCurrentFrameInlineDistanceFromBlock() {
  nscoord x = 0;
  for (const auto* psd = mCurrentSpan; psd; psd = psd->mParent) {
    x += psd->mICoord;
  }
  return x;
}

/**
 * This method syncs bounds of ruby annotations and ruby annotation
 * containers from their rect. It is necessary because:
 * Containers are not part of the line in their levels, which means
 * their bounds are not set properly before.
 * Ruby annotations' position may have been changed when reflowing
 * their containers.
 */
void nsLineLayout::SyncAnnotationBounds(PerFrameData* aRubyFrame) {
  MOZ_ASSERT(aRubyFrame->mFrame->IsRubyFrame());
  MOZ_ASSERT(aRubyFrame->mSpan);

  PerSpanData* span = aRubyFrame->mSpan;
  WritingMode lineWM = mRootSpan->mWritingMode;
  for (PerFrameData* pfd = span->mFirstFrame; pfd; pfd = pfd->mNext) {
    for (PerFrameData* rtc = pfd->mNextAnnotation; rtc;
         rtc = rtc->mNextAnnotation) {
      if (lineWM.IsOrthogonalTo(rtc->mFrame->GetWritingMode())) {
        // Inter-character case: don't attempt to sync annotation bounds.
        continue;
      }
      // When the annotation container is reflowed, the width of the
      // ruby container is unknown so we use a dummy container size;
      // in the case of RTL block direction, the final position will be
      // fixed up later.
      const nsSize dummyContainerSize;
      LogicalRect rtcBounds(lineWM, rtc->mFrame->GetRect(), dummyContainerSize);
      rtc->mBounds = rtcBounds;
      nsSize rtcSize = rtcBounds.Size(lineWM).GetPhysicalSize(lineWM);
      for (PerFrameData* rt = rtc->mSpan->mFirstFrame; rt; rt = rt->mNext) {
        LogicalRect rtBounds = rt->mFrame->GetLogicalRect(lineWM, rtcSize);
        MOZ_ASSERT(rt->mBounds.Size(lineWM) == rtBounds.Size(lineWM),
                   "Size of the annotation should not have been changed");
        rt->mBounds.SetOrigin(lineWM, rtBounds.Origin(lineWM));
      }
    }
  }
}

/**
 * See if the frame can be placed now that we know it's desired size.
 * We can always place the frame if the line is empty. Note that we
 * know that the reflow-status is not a break-before because if it was
 * ReflowFrame above would have returned false, preventing this method
 * from being called. The logic in this method assumes that.
 *
 * Note that there is no check against the Y coordinate because we
 * assume that the caller will take care of that.
 */
bool nsLineLayout::CanPlaceFrame(PerFrameData* pfd, bool aNotSafeToBreak,
                                 bool aFrameCanContinueTextRun,
                                 bool aCanRollBackBeforeFrame,
                                 ReflowOutput& aMetrics,
                                 nsReflowStatus& aStatus,
                                 bool* aOptionalBreakAfterFits) {
  MOZ_ASSERT(pfd && pfd->mFrame, "bad args, null pointers for frame data");

  *aOptionalBreakAfterFits = true;

  WritingMode lineWM = mRootSpan->mWritingMode;
  /*
   * We want to only apply the end margin if we're the last continuation and
   * either not in an {ib} split or the last inline in it.  In all other
   * cases we want to zero it out.  That means zeroing it out if any of these
   * conditions hold:
   * 1) The frame is not complete (in this case it will get a next-in-flow)
   * 2) The frame is complete but has a non-fluid continuation on its
   *    continuation chain.  Note that if it has a fluid continuation, that
   *    continuation will get destroyed later, so we don't want to drop the
   *    end-margin in that case.
   * 3) The frame is in an {ib} split and is not the last part.
   *
   * However, none of that applies if this is a letter frame (XXXbz why?)
   *
   * For box-decoration-break:clone we apply the end margin on all
   * continuations (that are not letter frames).
   */
  if ((aStatus.IsIncomplete() ||
       pfd->mFrame->LastInFlow()->GetNextContinuation() ||
       pfd->mFrame->FrameIsNonLastInIBSplit()) &&
      !pfd->mIsLetterFrame &&
      pfd->mFrame->StyleBorder()->mBoxDecorationBreak ==
          StyleBoxDecorationBreak::Slice) {
    pfd->mMargin.IEnd(lineWM) = 0;
  }

  // Apply the start margin to the frame bounds.
  nscoord startMargin = pfd->mMargin.IStart(lineWM);
  nscoord endMargin = pfd->mMargin.IEnd(lineWM);

  pfd->mBounds.IStart(lineWM) += startMargin;

  PerSpanData* psd = mCurrentSpan;
  if (psd->mNoWrap) {
    // When wrapping is off, everything fits.
    return true;
  }

#ifdef NOISY_CAN_PLACE_FRAME
  if (psd->mFrame) {
    psd->mFrame->mFrame->ListTag(stdout);
  }
  printf(": aNotSafeToBreak=%s frame=", aNotSafeToBreak ? "true" : "false");
  pfd->mFrame->ListTag(stdout);
  printf(" frameWidth=%d, margins=%d,%d\n",
         pfd->mBounds.IEnd(lineWM) + endMargin - psd->mICoord, startMargin,
         endMargin);
#endif

  // Set outside to true if the result of the reflow leads to the
  // frame sticking outside of our available area.
  bool outside =
      pfd->mBounds.IEnd(lineWM) - mTrimmableISize + endMargin > psd->mIEnd;
  if (!outside) {
    // If it fits, it fits
#ifdef NOISY_CAN_PLACE_FRAME
    printf("   ==> inside\n");
#endif
    return true;
  }
  *aOptionalBreakAfterFits = false;

  // When it doesn't fit, check for a few special conditions where we
  // allow it to fit anyway.
  if (0 == startMargin + pfd->mBounds.ISize(lineWM) + endMargin) {
    // Empty frames always fit right where they are
#ifdef NOISY_CAN_PLACE_FRAME
    printf("   ==> empty frame fits\n");
#endif
    return true;
  }

  // another special case:  always place a BR
  if (pfd->mFrame->IsBrFrame()) {
#ifdef NOISY_CAN_PLACE_FRAME
    printf("   ==> BR frame fits\n");
#endif
    return true;
  }

  if (aNotSafeToBreak) {
    // There are no frames on the line that take up width and the line is
    // not impacted by floats, so we must allow the current frame to be
    // placed on the line
#ifdef NOISY_CAN_PLACE_FRAME
    printf("   ==> not-safe and not-impacted fits: ");
    while (nullptr != psd) {
      printf("<psd=%p x=%d left=%d> ", psd, psd->mICoord, psd->mIStart);
      psd = psd->mParent;
    }
    printf("\n");
#endif
    return true;
  }

  // Special check for span frames
  if (pfd->mSpan && pfd->mSpan->mContainsFloat) {
    // If the span either directly or indirectly contains a float then
    // it fits. Why? It's kind of complicated, but here goes:
    //
    // 1. CanPlaceFrame is used for all frame placements on a line,
    // and in a span. This includes recursively placement of frames
    // inside of spans, and the span itself. Because the logic always
    // checks for room before proceeding (the code above here), the
    // only things on a line will be those things that "fit".
    //
    // 2. Before a float is placed on a line, the line has to be empty
    // (otherwise it's a "below current line" float and will be placed
    // after the line).
    //
    // Therefore, if the span directly or indirectly has a float
    // then it means that at the time of the placement of the float
    // the line was empty. Because of #1, only the frames that fit can
    // be added after that point, therefore we can assume that the
    // current span being placed has fit.
    //
    // So how do we get here and have a span that should already fit
    // and yet doesn't: Simple: span's that have the no-wrap attribute
    // set on them and contain a float and are placed where they
    // don't naturally fit.
    return true;
  }

  if (aFrameCanContinueTextRun) {
    // Let it fit, but we reserve the right to roll back.
    // Note that we usually won't get here because a text frame will break
    // itself to avoid exceeding the available width.
    // We'll only get here for text frames that couldn't break early enough.
#ifdef NOISY_CAN_PLACE_FRAME
    printf("   ==> placing overflowing textrun, requesting backup\n");
#endif

    // We will want to try backup.
    mNeedBackup = true;
    return true;
  }

#ifdef NOISY_CAN_PLACE_FRAME
  printf("   ==> didn't fit\n");
#endif
  aStatus.SetInlineLineBreakBeforeAndReset();
  return false;
}

/**
 * Place the frame. Update running counters.
 */
void nsLineLayout::PlaceFrame(PerFrameData* pfd, ReflowOutput& aMetrics) {
  WritingMode lineWM = mRootSpan->mWritingMode;

  // If the frame's block direction does not match the line's, we can't use
  // its ascent; instead, treat it as a block with baseline at the block-end
  // edge (or block-begin in the case of an "inverted" line).
  if (pfd->mWritingMode.GetBlockDir() != lineWM.GetBlockDir()) {
    pfd->mAscent = lineWM.IsAlphabeticalBaseline()
                       ? lineWM.IsLineInverted() ? 0 : aMetrics.BSize(lineWM)
                       : aMetrics.BSize(lineWM) / 2;
  } else {
    // For inline reflow participants, baseline may get assigned as the frame is
    // vertically aligned, which happens after this.
    const auto baselineSource = pfd->mFrame->StyleDisplay()->mBaselineSource;
    if (baselineSource == StyleBaselineSource::Auto ||
        pfd->mFrame->IsLineParticipant()) {
      if (aMetrics.BlockStartAscent() == ReflowOutput::ASK_FOR_BASELINE) {
        pfd->mAscent = pfd->mFrame->GetLogicalBaseline(lineWM);
      } else {
        pfd->mAscent = aMetrics.BlockStartAscent();
      }
    } else {
      const auto sourceGroup = [baselineSource]() {
        switch (baselineSource) {
          case StyleBaselineSource::First:
            return BaselineSharingGroup::First;
          case StyleBaselineSource::Last:
            return BaselineSharingGroup::Last;
          case StyleBaselineSource::Auto:
            break;
        }
        MOZ_ASSERT_UNREACHABLE("Auto should be already handled?");
        return BaselineSharingGroup::First;
      }();
      // We ignore line-layout specific layout quirks by setting
      // `BaselineExportContext::Other`.
      // Note(dshin): For a lot of frames, the export context does not make a
      // difference, and we may be wasting the value cached in
      // `BlockStartAscent`.
      pfd->mAscent = pfd->mFrame->GetLogicalBaseline(
          lineWM, sourceGroup, BaselineExportContext::Other);
    }
  }

  // Advance to next inline coordinate
  mCurrentSpan->mICoord = pfd->mBounds.IEnd(lineWM) + pfd->mMargin.IEnd(lineWM);

  // Count the number of non-placeholder frames on the line...
  if (pfd->mFrame->IsPlaceholderFrame()) {
    NS_ASSERTION(
        pfd->mBounds.ISize(lineWM) == 0 && pfd->mBounds.BSize(lineWM) == 0,
        "placeholders should have 0 width/height (checking "
        "placeholders were never counted by the old code in "
        "this function)");
  } else {
    mTotalPlacedFrames++;
  }
}

void nsLineLayout::AddMarkerFrame(nsIFrame* aFrame,
                                  const ReflowOutput& aMetrics) {
  MOZ_ASSERT(mCurrentSpan == mRootSpan, "bad linelayout user");
  MOZ_ASSERT(mGotLineBox, "must have line box");

  nsBlockFrame* blockFrame = do_QueryFrame(LineContainerFrame());
  MOZ_ASSERT(blockFrame, "must be for block");
  if (!blockFrame->MarkerIsEmpty(aFrame)) {
    mLineIsEmpty = false;
    mHasMarker = true;
    mLineBox->SetHasMarker();
  }

  WritingMode lineWM = mRootSpan->mWritingMode;
  PerFrameData* pfd = NewPerFrameData(aFrame);
  PerSpanData* psd = mRootSpan;

  MOZ_ASSERT(psd->mFirstFrame, "adding marker to an empty line?");
  // Prepend the marker frame to the line.
  psd->mFirstFrame->mPrev = pfd;
  pfd->mNext = psd->mFirstFrame;
  psd->mFirstFrame = pfd;

  pfd->mIsMarker = true;
  if (aMetrics.BlockStartAscent() == ReflowOutput::ASK_FOR_BASELINE) {
    pfd->mAscent = aFrame->GetLogicalBaseline(lineWM);
  } else {
    pfd->mAscent = aMetrics.BlockStartAscent();
  }

  // Note: block-coord value will be updated during block-direction alignment
  pfd->mBounds = LogicalRect(lineWM, aFrame->GetRect(), ContainerSize());
  pfd->mOverflowAreas = aMetrics.mOverflowAreas;
}

void nsLineLayout::RemoveMarkerFrame(nsIFrame* aFrame) {
  PerSpanData* psd = mCurrentSpan;
  MOZ_ASSERT(psd == mRootSpan, "::marker on non-root span?");
  MOZ_ASSERT(psd->mFirstFrame->mFrame == aFrame,
             "::marker is not the first frame?");
  PerFrameData* pfd = psd->mFirstFrame;
  MOZ_ASSERT(pfd != psd->mLastFrame, "::marker is the only frame?");
  pfd->mNext->mPrev = nullptr;
  psd->mFirstFrame = pfd->mNext;
  FreeFrame(pfd);
}

#ifdef DEBUG
void nsLineLayout::DumpPerSpanData(PerSpanData* psd, int32_t aIndent) {
  nsIFrame::IndentBy(stdout, aIndent);
  printf("%p: left=%d x=%d right=%d\n", static_cast<void*>(psd), psd->mIStart,
         psd->mICoord, psd->mIEnd);
  PerFrameData* pfd = psd->mFirstFrame;
  while (nullptr != pfd) {
    nsIFrame::IndentBy(stdout, aIndent + 1);
    pfd->mFrame->ListTag(stdout);
    nsRect rect =
        pfd->mBounds.GetPhysicalRect(psd->mWritingMode, ContainerSize());
    printf(" %d,%d,%d,%d\n", rect.x, rect.y, rect.width, rect.height);
    if (pfd->mSpan) {
      DumpPerSpanData(pfd->mSpan, aIndent + 1);
    }
    pfd = pfd->mNext;
  }
}
#endif

void nsLineLayout::RecordNoWrapFloat(nsIFrame* aFloat) {
  GetOutermostLineLayout()->mBlockRS->mNoWrapFloats.AppendElement(aFloat);
}

void nsLineLayout::FlushNoWrapFloats() {
  auto& noWrapFloats = GetOutermostLineLayout()->mBlockRS->mNoWrapFloats;
  for (nsIFrame* floatedFrame : noWrapFloats) {
    TryToPlaceFloat(floatedFrame);
  }
  noWrapFloats.Clear();
}

bool nsLineLayout::TryToPlaceFloat(nsIFrame* aFloat) {
  // Add mTrimmableISize to the available width since if the line ends here, the
  // width of the inline content will be reduced by mTrimmableISize.
  nscoord availableISize =
      mCurrentSpan->mIEnd - (mCurrentSpan->mICoord - mTrimmableISize);
  NS_ASSERTION(!(aFloat->IsLetterFrame() && GetFirstLetterStyleOK()),
               "FirstLetterStyle set on line with floating first letter");
  return GetOutermostLineLayout()->AddFloat(aFloat, availableISize);
}

bool nsLineLayout::NotifyOptionalBreakPosition(nsIFrame* aFrame,
                                               int32_t aOffset, bool aFits,
                                               gfxBreakPriority aPriority) {
  NS_ASSERTION(!aFits || !mNeedBackup,
               "Shouldn't be updating the break position with a break that fits"
               " after we've already flagged an overrun");
  MOZ_ASSERT(mCurrentSpan, "Should be doing line layout");
  if (mCurrentSpan->mNoWrap) {
    FlushNoWrapFloats();
  }

  // Remember the last break position that fits; if there was no break that fit,
  // just remember the first break
  if ((aFits && aPriority >= mLastOptionalBreakPriority) ||
      !mLastOptionalBreakFrame) {
    mLastOptionalBreakFrame = aFrame;
    mLastOptionalBreakFrameOffset = aOffset;
    mLastOptionalBreakPriority = aPriority;
  }
  return aFrame && mForceBreakFrame == aFrame &&
         mForceBreakFrameOffset == aOffset;
}

#define VALIGN_OTHER 0
#define VALIGN_TOP 1
#define VALIGN_BOTTOM 2
#define VALIGN_CENTER 3

void nsLineLayout::SetSpanForEmptyLine(PerSpanData* aPerSpanData,
                                       WritingMode aWM,
                                       const nsSize& aContainerSize,
                                       nscoord aBStartEdge) {
  for (PerFrameData* pfd = aPerSpanData->mFirstFrame; pfd; pfd = pfd->mNext) {
    // Ideally, if the frame would collapse itself - but it depends on
    // knowing that the line is empty.
    if (!pfd->mFrame->IsInlineFrame() && !pfd->mFrame->IsRubyFrame() &&
        !pfd->mFrame->IsPlaceholderFrame()) {
      continue;
    }
    // Collapse the physical size to 0.
    pfd->mBounds.BStart(aWM) = aBStartEdge;
    pfd->mBounds.BSize(aWM) = 0;
    // Initialize mBlockDirAlign (though it doesn't make much difference
    // because we don't align empty boxes).
    pfd->mBlockDirAlign = VALIGN_OTHER;
    pfd->mFrame->SetRect(aWM, pfd->mBounds, aContainerSize);
    if (pfd->mSpan) {
      // For child spans, the block-start edge is relative to that of parent,
      // which is zero (since it is empty). See NOTE in
      // `nsLineLayout::ReflowFrame`.
      SetSpanForEmptyLine(pfd->mSpan, aWM, aContainerSize, 0);
    }
  }
}

struct TextBoxEdgeMetrics {
  nscoord mOver;
  nscoord mUnder;
};

// Resolves the over/under metrics corresponding to the frame's text-box-edge.
static TextBoxEdgeMetrics ResolveTextBoxEdgeMetrics(
    const StyleTextBoxEdge& aTextBoxEdge, nsFontMetrics* aFontMetrics) {
  TextBoxEdgeMetrics result;
  StyleTextEdgeKeyword over, under;
  if (aTextBoxEdge.IsTextEdge()) {
    const StyleTextEdge& textEdge = aTextBoxEdge.AsTextEdge();
    over = textEdge.over;
    under = textEdge.under;
  } else {
    over = under = StyleTextEdgeKeyword::Text;
  }

  switch (over) {
    case StyleTextEdgeKeyword::Cap:
      result.mOver = aFontMetrics->CapHeight();
      break;
    case StyleTextEdgeKeyword::Ex:
      result.mOver = aFontMetrics->XHeight();
      break;
    case StyleTextEdgeKeyword::Ideographic:
      result.mOver = aFontMetrics->IdeographicOverBaseline();
      break;
    case StyleTextEdgeKeyword::IdeographicInk:
      result.mOver = aFontMetrics->IdeographicInkOverBaseline();
      break;
    default:
    case StyleTextEdgeKeyword::Text:
      result.mOver = aFontMetrics->MaxAscent();
      break;
  }

  switch (under) {
    case StyleTextEdgeKeyword::Alphabetic:
      result.mUnder = -aFontMetrics->AlphabeticBaseline();
      break;
    case StyleTextEdgeKeyword::Ideographic:
      result.mUnder = -aFontMetrics->IdeographicUnderBaseline();
      break;
    case StyleTextEdgeKeyword::IdeographicInk:
      result.mUnder = -aFontMetrics->IdeographicInkUnderBaseline();
      break;
    default:
    case StyleTextEdgeKeyword::Text:
      result.mUnder = aFontMetrics->MaxDescent();
      break;
  }

  return result;
}

void nsLineLayout::ApplyBlockTextBoxTrim(PerSpanData* psd, WritingMode aLineWM,
                                         nscoord* aLineBSize,
                                         nscoord* aBaselineBCoord,
                                         nsFlowAreaRect* aFlowArea,
                                         bool aIsLastFormattedLine) {
  MOZ_ASSERT(psd == mRootSpan);
  MOZ_ASSERT(mBlockRS);
  nsIFrame* blockFrame = psd->mFrame->mFrame;
  const bool shouldApplyTrimStart =
      mBlockRS->mFlags.mShouldApplyTextBoxTrimStart && mLineNumber == 0;
  const bool shouldApplyTrimEnd =
      (mBlockRS->mFlags.mShouldApplyTextBoxTrimAtBlockEnd &&
       aIsLastFormattedLine) ||
      mLineBox->TextBoxTrimEndForced();
  const bool shouldComputeTrimEnd =
      (shouldApplyTrimEnd ||
       mBlockRS->mFlags.mShouldApplyTextBoxTrimAtFragmentEnd);

  if (!shouldApplyTrimStart && !shouldComputeTrimEnd) {
    return;
  }

  // Identify the current total height above and below the baseline,
  // to be compared against the target text-box-edge metrics to determine
  // how much to trim on each side.
  nscoord totalOver = *aBaselineBCoord - mBStartEdge;
  nscoord totalUnder = *aLineBSize - totalOver;

  // The line should be trimmed to the specified font metric from the block's
  // root inline box. Gecko doesn't construct an actual frame for the root
  // inline box (which is generally unstyleable), so the metric normally comes
  // from the block itself. However, a ::first-line declaration block *can*
  // style the fragment of the root inline box that it contains and should be
  // used to determine the trim metric.
  nsIFrame* metricsFrame = blockFrame;
  if (psd->mFirstFrame && psd->mFirstFrame->mFrame->Style()->GetPseudoType() ==
                              PseudoStyleType::FirstLine) {
    metricsFrame = psd->mFirstFrame->mFrame;
  }

  const StyleTextBoxEdge& textBoxEdge = metricsFrame->StyleText()->mTextBoxEdge;
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(metricsFrame);
  const auto [trimmedOver, trimmedUnder] =
      ResolveTextBoxEdgeMetrics(textBoxEdge, fm);

  if (shouldApplyTrimStart) {
    // Trim on the start side by moving the block start coordinate
    // and the baseline coordinate up by the trim amount. Additionally
    // move all child frames and any positioned floats on this line
    // up by the trim amount.
    const nscoord trimAmount = aLineWM.IsLineInverted()
                                   ? totalUnder - trimmedUnder
                                   : totalOver - trimmedOver;
    mBStartEdge -= trimAmount;
    *aBaselineBCoord -= trimAmount;
    for (PerFrameData* pfd = psd->mFirstFrame; pfd; pfd = pfd->mNext) {
      pfd->mBounds.BStart(aLineWM) -= trimAmount;
      pfd->mFrame->SetRect(aLineWM, pfd->mBounds, ContainerSize());
    }
    if (aFlowArea) {
      aFlowArea->mRect.BStart(aLineWM) -= trimAmount;
    }
    for (nsIFrame* floatFrame : mBlockRS->mCurrentLineFloats) {
      floatFrame->MovePositionBy(
          LogicalPoint(aLineWM, 0, -trimAmount)
              .GetPhysicalPoint(aLineWM, ContainerSize()));
    }
    mLineBox->SetTextBoxTrimStartApplied();
  }

  if (shouldComputeTrimEnd) {
    mPotentialTextBoxTrimEndAmount = aLineWM.IsLineInverted()
                                         ? totalOver - trimmedOver
                                         : totalUnder - trimmedUnder;
    if (shouldApplyTrimEnd) {
      // Trim on the end side by reducing the block size of the line box.
      *aLineBSize -= mPotentialTextBoxTrimEndAmount;
      mLineBox->SetTextBoxTrimEndApplied();
      mLineBox->ClearTextBoxTrimEndForced();
    }
  }
}

void nsLineLayout::VerticalAlignLine(nsFlowAreaRect* aFlowArea,
                                     bool aIsLastFormattedLine) {
  // Partially place the children of the block frame. The baseline for
  // this operation is set to zero so that the y coordinates for all
  // of the placed children will be relative to there.
  PerSpanData* psd = mRootSpan;
  if (mLineIsEmpty) {
    // This line is empty, and should be consisting of only inline elements.
    // (inline-block elements would make the line non-empty).
    SetSpanForEmptyLine(psd, mRootSpan->mWritingMode, ContainerSize(),
                        mBStartEdge);

    mFinalLineBSize = 0;
    if (mGotLineBox) {
      mLineBox->SetBounds(psd->mWritingMode, psd->mIStart, mBStartEdge,
                          psd->mICoord - psd->mIStart, 0, ContainerSize());

      mLineBox->SetLogicalAscent(0);
    }
    return;
  }
  VerticalAlignFrames(psd);

  // *** Note that comments here still use the anachronistic term
  // "line-height" when we really mean "size of the line in the block
  // direction", "vertical-align" when we really mean "alignment in
  // the block direction", and "top" and "bottom" when we really mean
  // "block start" and "block end". This is partly for brevity and
  // partly to retain the association with the CSS line-height and
  // vertical-align properties.
  //
  // Compute the line-height. The line-height will be the larger of:
  //
  // [1] maxBCoord - minBCoord (the distance between the first child's
  // block-start edge and the last child's block-end edge)
  //
  // [2] the maximum logical box block size (since not every frame may have
  // participated in #1; for example: "top" and "botttom" aligned frames)
  //
  // [3] the minimum line height ("line-height" property set on the
  // block frame)
  nscoord lineBSize = psd->mMaxBCoord - psd->mMinBCoord;

  // Now that the line-height is computed, we need to know where the
  // baseline is in the line. Position baseline so that mMinBCoord is just
  // inside the start of the line box.
  nscoord baselineBCoord = mBStartEdge - std::min(0, psd->mMinBCoord);

  // It's also possible that the line block-size isn't tall enough because
  // of "top" and "bottom" aligned elements that were not accounted for in
  // min/max BCoord.
  //
  // The CSS2 spec doesn't really say what happens when to the
  // baseline in this situations. What we do is if the largest start
  // aligned box block size is greater than the line block-size then we leave
  // the baseline alone. If the largest end aligned box is greater
  // than the line block-size then we slide the baseline forward by the extra
  // amount.
  //
  // Navigator 4 gives precedence to the first top/bottom aligned
  // object.  We just let block end aligned objects win.
  if (lineBSize < mMaxEndBoxBSize) {
    // When the line is shorter than the maximum block start aligned box
    nscoord extra = mMaxEndBoxBSize - lineBSize;
    baselineBCoord += extra;
    lineBSize = mMaxEndBoxBSize;
  }
  lineBSize = std::max(lineBSize, mMaxStartBoxBSize);
#ifdef NOISY_BLOCKDIR_ALIGN
  printf("  [line]==> lineBSize=%d baselineBCoord=%d\n", lineBSize,
         baselineBCoord);
#endif

  // Now position all of the frames in the root span. We will also
  // recurse over the child spans and place any frames we find with
  // vertical-align: top/bottom/center.
  // XXX PERFORMANCE: set a bit per-span to avoid the extra work
  // (propagate it upward too)
  WritingMode lineWM = psd->mWritingMode;
  for (PerFrameData* pfd = psd->mFirstFrame; pfd; pfd = pfd->mNext) {
    if (pfd->mBlockDirAlign == VALIGN_OTHER) {
      pfd->mBounds.BStart(lineWM) += baselineBCoord;
      pfd->mFrame->SetRect(lineWM, pfd->mBounds, ContainerSize());
    }
  }
  PlaceTopBottomCenterFrames(psd, -mBStartEdge, lineBSize);

  if (mGotLineBox) {
    if (mBlockRS) {
      ApplyBlockTextBoxTrim(psd, lineWM, &lineBSize, &baselineBCoord, aFlowArea,
                            aIsLastFormattedLine);
    }

    // Fill in returned line-box and max-element-width data
    mLineBox->SetBounds(lineWM, psd->mIStart, mBStartEdge,
                        psd->mICoord - psd->mIStart, lineBSize,
                        ContainerSize());

    mLineBox->SetLogicalAscent(baselineBCoord - mBStartEdge);
#ifdef NOISY_BLOCKDIR_ALIGN
    printf("  [line]==> bounds{x,y,w,h}={%d,%d,%d,%d} lh=%d a=%d\n",
           mLineBox->GetBounds().IStart(lineWM),
           mLineBox->GetBounds().BStart(lineWM),
           mLineBox->GetBounds().ISize(lineWM),
           mLineBox->GetBounds().BSize(lineWM), lineBSize,
           mLineBox->GetLogicalAscent());
#endif
  }

  mFinalLineBSize = lineBSize;
}

nscoord nsLineLayout::ComputeTopAlignFrameStart(const PerFrameData* pfd,
                                                const WritingMode& aWM,
                                                nscoord aDistanceFromStart,
                                                nscoord aLineBSize) {
  if (PerSpanData* span = pfd->mSpan) {
    return -aDistanceFromStart - span->mMinBCoord;
  } else {
    return -aDistanceFromStart + pfd->mMargin.BStart(aWM);
  }
}

nscoord nsLineLayout::ComputeBottomAlignFrameStart(const PerFrameData* pfd,
                                                   const WritingMode& aWM,
                                                   nscoord aDistanceFromStart,
                                                   nscoord aLineBSize) {
  if (PerSpanData* span = pfd->mSpan) {
    // Compute bottom leading
    return -aDistanceFromStart + aLineBSize - span->mMaxBCoord;
  } else {
    return -aDistanceFromStart + aLineBSize - pfd->mMargin.BEnd(aWM) -
           pfd->mBounds.BSize(aWM);
  }
}

// Place frames with CSS property vertical-align: top/bottom/center.
void nsLineLayout::PlaceTopBottomCenterFrames(PerSpanData* psd,
                                              nscoord aDistanceFromStart,
                                              nscoord aLineBSize) {
  for (PerFrameData* pfd = psd->mFirstFrame; pfd; pfd = pfd->mNext) {
    PerSpanData* span = pfd->mSpan;
#ifdef DEBUG
    NS_ASSERTION(0xFF != pfd->mBlockDirAlign, "umr");
#endif
    WritingMode lineWM = mRootSpan->mWritingMode;
    nsSize containerSize = ContainerSizeForSpan(psd);
    switch (pfd->mBlockDirAlign) {
      case VALIGN_TOP:
        pfd->mBounds.BStart(lineWM) = ComputeTopAlignFrameStart(
            pfd, lineWM, aDistanceFromStart, aLineBSize);
        pfd->mFrame->SetRect(lineWM, pfd->mBounds, containerSize);
#ifdef NOISY_BLOCKDIR_ALIGN
        printf("    ");
        pfd->mFrame->ListTag(stdout);
        printf(": y=%d dTop=%d [bp.top=%d topLeading=%d]\n",
               pfd->mBounds.BStart(lineWM), aDistanceFromStart,
               span ? pfd->mBorderPadding.BStart(lineWM) : 0,
               span ? span->mBStartLeading : 0);
#endif
        break;
      case VALIGN_BOTTOM:
        pfd->mBounds.BStart(lineWM) = ComputeBottomAlignFrameStart(
            pfd, lineWM, aDistanceFromStart, aLineBSize);
        pfd->mFrame->SetRect(lineWM, pfd->mBounds, containerSize);
#ifdef NOISY_BLOCKDIR_ALIGN
        printf("    ");
        pfd->mFrame->ListTag(stdout);
        printf(": y=%d\n", pfd->mBounds.BStart(lineWM));
#endif
        break;
      case VALIGN_CENTER:
        nscoord startTop = ComputeTopAlignFrameStart(
            pfd, lineWM, aDistanceFromStart, aLineBSize);
        nscoord startBottom = ComputeBottomAlignFrameStart(
            pfd, lineWM, aDistanceFromStart, aLineBSize);
        pfd->mBounds.BStart(lineWM) = (startTop + startBottom) / 2;
        pfd->mFrame->SetRect(lineWM, pfd->mBounds, containerSize);
        break;
    }
    if (span) {
      nscoord fromStart = aDistanceFromStart + pfd->mBounds.BStart(lineWM);
      PlaceTopBottomCenterFrames(span, fromStart, aLineBSize);
    }
  }
}

static nscoord GetBSizeOfEmphasisMarks(nsIFrame* aSpanFrame, float aInflation) {
  RefPtr<nsFontMetrics> fm = nsLayoutUtils::GetFontMetricsOfEmphasisMarks(
      aSpanFrame->Style(), aSpanFrame->PresContext(), aInflation);
  return aSpanFrame->PresContext()->NormalizeRubyMetrics()
             ? (fm->TrimmedAscent() + fm->TrimmedDescent()) *
                   aSpanFrame->PresContext()->RubyPositioningFactor()
             : fm->MaxHeight();
}

void nsLineLayout::AdjustLeadings(nsIFrame* spanFrame, PerSpanData* psd,
                                  const nsStyleText* aStyleText,
                                  float aInflation,
                                  bool* aZeroEffectiveSpanBox) {
  MOZ_ASSERT(spanFrame == psd->mFrame->mFrame);
  nscoord requiredStartLeading = 0;
  nscoord requiredEndLeading = 0;
  if (spanFrame->IsRubyFrame()) {
    // We may need to extend leadings here for ruby annotations as
    // required by section Line Spacing in the CSS Ruby spec.
    // See http://dev.w3.org/csswg/css-ruby/#line-height
    auto rubyFrame = static_cast<nsRubyFrame*>(spanFrame);
    RubyBlockLeadings leadings = rubyFrame->GetBlockLeadings();
    requiredStartLeading += leadings.mStart;
    requiredEndLeading += leadings.mEnd;
  }
  if (aStyleText->HasEffectiveTextEmphasis()) {
    nscoord bsize = GetBSizeOfEmphasisMarks(spanFrame, aInflation);
    LogicalSide side = aStyleText->TextEmphasisSide(
        mRootSpan->mWritingMode, spanFrame->StyleFont()->GetLangAtom());
    if (spanFrame->PresContext()->NormalizeRubyMetrics()) {
      // Add extra leading for emphasis marks only if their bsize exceeds the
      // space built in to the font (difference between its max ascent/descent
      // and the em-normalized metrics that are used to position the mark).
      RefPtr fm = nsLayoutUtils::GetInflatedFontMetricsForFrame(spanFrame);
      float factor = spanFrame->PresContext()->RubyPositioningFactor();
      if (side == LogicalSide::BStart) {
        requiredStartLeading += std::max(
            0, bsize - (fm->MaxAscent() -
                        nscoord(NS_round(factor * fm->TrimmedAscent()))));
      } else {
        requiredEndLeading += std::max(
            0, bsize - (fm->MaxDescent() -
                        nscoord(NS_round(factor * fm->TrimmedDescent()))));
      }
    } else {
      if (side == LogicalSide::BStart) {
        requiredStartLeading += bsize;
      } else {
        MOZ_ASSERT(side == LogicalSide::BEnd,
                   "emphasis marks must be in block axis");
        requiredEndLeading += bsize;
      }
    }
  }

  nscoord requiredLeading = requiredStartLeading + requiredEndLeading;
  // If we do not require any additional leadings, don't touch anything
  // here even if it is greater than the original leading, because the
  // latter could be negative.
  if (requiredLeading != 0) {
    nscoord leading = psd->mBStartLeading + psd->mBEndLeading;
    nscoord deltaLeading = requiredLeading - leading;
    if (deltaLeading > 0) {
      // If the total leading is not wide enough for ruby annotations
      // and/or emphasis marks, extend the side which is not enough. If
      // both sides are not wide enough, replace the leadings with the
      // requested values.
      if (requiredStartLeading < psd->mBStartLeading) {
        psd->mBEndLeading += deltaLeading;
      } else if (requiredEndLeading < psd->mBEndLeading) {
        psd->mBStartLeading += deltaLeading;
      } else {
        psd->mBStartLeading = requiredStartLeading;
        psd->mBEndLeading = requiredEndLeading;
      }
      psd->mLogicalBSize += deltaLeading;
      // We have adjusted the leadings, it is no longer a zero
      // effective span box.
      *aZeroEffectiveSpanBox = false;
    }
  }
}

static float GetInflationForBlockDirAlignment(nsIFrame* aFrame,
                                              nscoord aInflationMinFontSize) {
  if (aFrame->IsInSVGTextSubtree()) {
    const nsIFrame* container =
        nsLayoutUtils::GetClosestFrameOfType(aFrame, LayoutFrameType::SVGText);
    NS_ASSERTION(container, "expected to find an ancestor SVGTextFrame");
    return static_cast<const SVGTextFrame*>(container)
        ->GetFontSizeScaleFactor();
  }
  return nsLayoutUtils::FontSizeInflationInner(aFrame, aInflationMinFontSize);
}

bool nsLineLayout::ShouldApplyLineHeightInPreserveWhiteSpace(
    const PerSpanData* psd) {
  if (psd->mFrame->mFrame->Style()->IsAnonBox()) {
    // e.g. An empty `input[type=button]` should still be line-height sized.
    return true;
  }

  for (PerFrameData* pfd = psd->mFirstFrame; pfd; pfd = pfd->mNext) {
    if (!pfd->mIsEmpty) {
      return true;
    }
  }
  return false;
}

#define BLOCKDIR_ALIGN_FRAMES_NO_MINIMUM nscoord_MAX
#define BLOCKDIR_ALIGN_FRAMES_NO_MAXIMUM nscoord_MIN

// Place frames in the block direction within a given span (CSS property
// vertical-align) Note: this doesn't place frames with vertical-align:
// top/bottom/center as those have to wait until the entire line box block
// size is known. This is called after the span frame has finished being
// reflowed so that we know its block size.
void nsLineLayout::VerticalAlignFrames(PerSpanData* psd) {
  // Get parent frame info
  PerFrameData* spanFramePFD = psd->mFrame;
  nsIFrame* spanFrame = spanFramePFD->mFrame;

  // Get the parent frame's font for all of the frames in this span
  float inflation =
      GetInflationForBlockDirAlignment(spanFrame, mInflationMinFontSize);
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetFontMetricsForFrame(spanFrame, inflation);

  bool preMode = mStyleText->WhiteSpaceIsSignificant();

  // See if the span is an empty continuation. It's an empty continuation iff:
  // - it has a prev-in-flow
  // - it has no next in flow
  // - it's zero sized
  WritingMode lineWM = mRootSpan->mWritingMode;
  bool emptyContinuation = psd != mRootSpan && spanFrame->GetPrevInFlow() &&
                           !spanFrame->GetNextInFlow() &&
                           spanFramePFD->mBounds.IsZeroSize();

#ifdef NOISY_BLOCKDIR_ALIGN
  printf("[%sSpan]", (psd == mRootSpan) ? "Root" : "");
  spanFrame->ListTag(stdout);
  printf(": preMode=%s strictMode=%s w/h=%d,%d emptyContinuation=%s",
         preMode ? "yes" : "no",
         mPresContext->CompatibilityMode() != eCompatibility_NavQuirks ? "yes"
                                                                       : "no",
         spanFramePFD->mBounds.ISize(lineWM),
         spanFramePFD->mBounds.BSize(lineWM), emptyContinuation ? "yes" : "no");
  if (psd != mRootSpan) {
    printf(" bp=%d,%d,%d,%d margin=%d,%d,%d,%d",
           spanFramePFD->mBorderPadding.Top(lineWM),
           spanFramePFD->mBorderPadding.Right(lineWM),
           spanFramePFD->mBorderPadding.Bottom(lineWM),
           spanFramePFD->mBorderPadding.Left(lineWM),
           spanFramePFD->mMargin.Top(lineWM),
           spanFramePFD->mMargin.Right(lineWM),
           spanFramePFD->mMargin.Bottom(lineWM),
           spanFramePFD->mMargin.Left(lineWM));
  }
  printf("\n");
#endif

  // Compute the span's zeroEffectiveSpanBox flag. What we are trying
  // to determine is how we should treat the span: should it act
  // "normally" according to css2 or should it effectively
  // "disappear".
  //
  // In general, if the document being processed is in full standards
  // mode then it should act normally (with one exception). The
  // exception case is when a span is continued and yet the span is
  // empty (e.g. compressed whitespace). For this kind of span we treat
  // it as if it were not there so that it doesn't impact the
  // line block-size.
  //
  // In almost standards mode or quirks mode, we should sometimes make
  // it disappear. The cases that matter are those where the span
  // contains no real text elements that would provide an ascent and
  // descent and height. However, if css style elements have been
  // applied to the span (border/padding/margin) so that it's clear the
  // document author is intending css2 behavior then we act as if strict
  // mode is set.
  //
  // This code works correctly for preMode, because a blank line
  // in PRE mode is encoded as a text node with a LF in it, since
  // text nodes with only whitespace are considered in preMode.
  //
  // Much of this logic is shared with the various implementations of
  // nsIFrame::IsEmpty since they need to duplicate the way it makes
  // some lines empty.  However, nsIFrame::IsEmpty can't be reused here
  // since this code sets zeroEffectiveSpanBox even when there are
  // non-empty children.
  bool zeroEffectiveSpanBox = false;
  // XXXldb If we really have empty continuations, then all these other
  // checks don't make sense for them.
  // XXXldb This should probably just use nsIFrame::IsSelfEmpty, assuming that
  // it agrees with this code.  (If it doesn't agree, it probably should.)
  if ((emptyContinuation ||
       mPresContext->CompatibilityMode() != eCompatibility_FullStandards) &&
      ((psd == mRootSpan) || (spanFramePFD->mBorderPadding.IsAllZero() &&
                              spanFramePFD->mMargin.IsAllZero()))) {
    // This code handles an issue with compatibility with non-css
    // conformant browsers. In particular, there are some cases
    // where the font-size and line-height for a span must be
    // ignored and instead the span must *act* as if it were zero
    // sized. In general, if the span contains any non-compressed
    // text then we don't use this logic.
    // However, this is not propagated outwards, since (in compatibility
    // mode) we don't want big line heights for things like
    // <p><font size="-1">Text</font></p>

    // We shouldn't include any whitespace that collapses, unless we're
    // preformatted (in which case it shouldn't, but the width=0 test is
    // perhaps incorrect).  This includes whitespace at the beginning of
    // a line and whitespace preceded (?) by other whitespace.
    // See bug 134580 and bug 155333.
    zeroEffectiveSpanBox = true;
    for (PerFrameData* pfd = psd->mFirstFrame; pfd; pfd = pfd->mNext) {
      if (pfd->mIsTextFrame &&
          (pfd->mIsNonWhitespaceTextFrame || preMode ||
           pfd->mBounds.ISize(mRootSpan->mWritingMode) != 0)) {
        zeroEffectiveSpanBox = false;
        break;
      }
    }
  }

  // Setup baselineBCoord, minBCoord, and maxBCoord
  nscoord baselineBCoord, minBCoord, maxBCoord;
  if (psd == mRootSpan) {
    // Use a zero baselineBCoord since we don't yet know where the baseline
    // will be (until we know how tall the line is; then we will
    // know). In addition, use extreme values for the minBCoord and maxBCoord
    // values so that only the child frames will impact their values
    // (since these are children of the block, there is no span box to
    // provide initial values).
    baselineBCoord = 0;
    minBCoord = BLOCKDIR_ALIGN_FRAMES_NO_MINIMUM;
    maxBCoord = BLOCKDIR_ALIGN_FRAMES_NO_MAXIMUM;
#ifdef NOISY_BLOCKDIR_ALIGN
    printf("[RootSpan]");
    spanFrame->ListTag(stdout);
    printf(
        ": pass1 valign frames: topEdge=%d minLineBSize=%d "
        "zeroEffectiveSpanBox=%s\n",
        mBStartEdge, mMinLineBSize, zeroEffectiveSpanBox ? "yes" : "no");
#endif
  } else {
    // Compute the logical block size for this span. The logical block size
    // is based on the "line-height" value, not the font-size. Also
    // compute the top leading.
    float inflation =
        GetInflationForBlockDirAlignment(spanFrame, mInflationMinFontSize);
    nscoord logicalBSize = ReflowInput::CalcLineHeight(
        *spanFrame->Style(), spanFrame->PresContext(), spanFrame->GetContent(),
        inflation);
    nscoord contentBSize = spanFramePFD->mBounds.BSize(lineWM) -
                           spanFramePFD->mBorderPadding.BStartEnd(lineWM);

    // Special-case for a ::first-letter frame, set the line height to
    // the frame block size if the user has left line-height == normal
    if (spanFramePFD->mIsLetterFrame && !spanFrame->GetPrevInFlow() &&
        spanFrame->StyleFont()->mLineHeight.IsNormal()) {
      logicalBSize = spanFramePFD->mBounds.BSize(lineWM);
    }

    nscoord leading = logicalBSize - contentBSize;
    psd->mBStartLeading = leading / 2;
    psd->mBEndLeading = leading - psd->mBStartLeading;
    psd->mLogicalBSize = logicalBSize;
    AdjustLeadings(spanFrame, psd, spanFrame->StyleText(), inflation,
                   &zeroEffectiveSpanBox);

    if (zeroEffectiveSpanBox) {
      // When the span-box is to be ignored, zero out the initial
      // values so that the span doesn't impact the final line
      // height. The contents of the span can impact the final line
      // height.

      // Note that things are readjusted for this span after its children
      // are reflowed
      minBCoord = BLOCKDIR_ALIGN_FRAMES_NO_MINIMUM;
      maxBCoord = BLOCKDIR_ALIGN_FRAMES_NO_MAXIMUM;
    } else {
      // The initial values for the min and max block coord values are in the
      // span's coordinate space, and cover the logical block size of the span.
      // If there are child frames in this span that stick out of this area
      // then the minBCoord and maxBCoord are updated by the amount of logical
      // blockSize that is outside this range.
      minBCoord =
          spanFramePFD->mBorderPadding.BStart(lineWM) - psd->mBStartLeading;
      maxBCoord = minBCoord + psd->mLogicalBSize;
    }

    // This is the distance from the top edge of the parents visual
    // box to the baseline. The span already computed this for us,
    // so just use it.
    *psd->mBaseline = baselineBCoord = spanFramePFD->mAscent;

#ifdef NOISY_BLOCKDIR_ALIGN
    printf("[%sSpan]", (psd == mRootSpan) ? "Root" : "");
    spanFrame->ListTag(stdout);
    printf(
        ": baseLine=%d logicalBSize=%d topLeading=%d h=%d bp=%d,%d "
        "zeroEffectiveSpanBox=%s\n",
        baselineBCoord, psd->mLogicalBSize, psd->mBStartLeading,
        spanFramePFD->mBounds.BSize(lineWM),
        spanFramePFD->mBorderPadding.Top(lineWM),
        spanFramePFD->mBorderPadding.Bottom(lineWM),
        zeroEffectiveSpanBox ? "yes" : "no");
#endif
  }

  nscoord maxStartBoxBSize = 0;
  nscoord maxEndBoxBSize = 0;
  PerFrameData* pfd = psd->mFirstFrame;
  while (nullptr != pfd) {
    nsIFrame* frame = pfd->mFrame;

    // sanity check (see bug 105168, non-reproducible crashes from null frame)
    NS_ASSERTION(frame,
                 "null frame in PerFrameData - something is very very bad");
    if (!frame) {
      return;
    }

    // Compute the logical block size of the frame
    nscoord logicalBSize;
    PerSpanData* frameSpan = pfd->mSpan;
    if (frameSpan) {
      // For span frames the logical-block-size and start-leading were
      // pre-computed when the span was reflowed.
      logicalBSize = frameSpan->mLogicalBSize;
    } else {
      // For other elements the logical block size is the same as the
      // frame's block size plus its margins.
      logicalBSize =
          pfd->mBounds.BSize(lineWM) + pfd->mMargin.BStartEnd(lineWM);
      if (logicalBSize < 0 &&
          mPresContext->CompatibilityMode() != eCompatibility_FullStandards) {
        pfd->mAscent -= logicalBSize;
        logicalBSize = 0;
      }
    }

    // Get block-direction alignment values. For text frames, don't perform any
    // special baseline alignment; their inline container box is responsible for
    // performing baseline alignment.
    StyleAlignmentBaseline alignmentBaseline = StyleAlignmentBaseline::Baseline;
    if (!pfd->mIsTextFrame) {
      alignmentBaseline = frame->AlignmentBaseline();
    }

    const StyleBaselineShift& baselineShift =
        frame->StyleDisplay()->mBaselineShift;
    Maybe<StyleBaselineShiftKeyword> baselineShiftEnum =
        baselineShift.IsKeyword() ? Some(baselineShift.AsKeyword()) : Nothing();

#ifdef NOISY_BLOCKDIR_ALIGN
    printf("  [frame]");
    frame->ListTag(stdout);
    printf(": alignmentBaseline=%d baselineShiftIsKw=%d (enum == %d)\n",
           static_cast<int>(alignmentBaseline), baselineShiftEnum ? 1 : 0,
           baselineShiftEnum ? static_cast<int>(*baselineShiftEnum) : -1);
#endif

    // Apply writing mode transforms to the alignment properties
    if (lineWM.IsVertical()) {
      // For vertical writing mode where the dominant baseline is centered
      // (i.e. text-orientation is not sideways-*), we remap 'middle' to
      // 'middle-with-baseline' so that images align sensibly with the
      // center-baseline-aligned text.
      if (alignmentBaseline == StyleAlignmentBaseline::Middle &&
          !lineWM.IsSideways()) {
        alignmentBaseline = StyleAlignmentBaseline::MozMiddleWithBaseline;
      }

      // Swap the meanings of top and bottom when line is inverted
      // relative to block direction.
      if (lineWM.IsLineInverted()) {
        switch (alignmentBaseline) {
          case StyleAlignmentBaseline::TextTop:
            alignmentBaseline = StyleAlignmentBaseline::TextBottom;
            break;
          case StyleAlignmentBaseline::TextBottom:
            alignmentBaseline = StyleAlignmentBaseline::TextTop;
            break;
          default:
            break;
        }

        if (baselineShiftEnum) {
          switch (*baselineShiftEnum) {
            case StyleBaselineShiftKeyword::Top:
              baselineShiftEnum = Some(StyleBaselineShiftKeyword::Bottom);
              break;
            case StyleBaselineShiftKeyword::Bottom:
              baselineShiftEnum = Some(StyleBaselineShiftKeyword::Top);
              break;
            default:
              break;
          }
        }
      }
    }

    const auto GetFontBaseline = [](nsFontMetrics* aFM,
                                    StyleAlignmentBaseline aAlignmentBaseline) {
      switch (aAlignmentBaseline) {
        case StyleAlignmentBaseline::Alphabetic:
          return aFM->AlphabeticBaseline();
        case StyleAlignmentBaseline::Central:
          return aFM->CentralBaseline();
        case StyleAlignmentBaseline::Ideographic:
          return aFM->IdeographicUnderBaseline();
        case StyleAlignmentBaseline::Mathematical:
          return aFM->MathBaseline();
        case StyleAlignmentBaseline::Hanging:
          return aFM->HangingBaseline();
        default:
          MOZ_ASSERT_UNREACHABLE("Unexpected alignment baseline");
          return 0;
      }
    };

    // Adjust the bounds according to the given baseline.
    switch (alignmentBaseline) {
      case StyleAlignmentBaseline::Baseline:
        pfd->mBounds.BStart(lineWM) = baselineBCoord - pfd->mAscent;
        pfd->mBlockDirAlign = VALIGN_OTHER;
        break;

      default:
      case StyleAlignmentBaseline::Alphabetic:
      case StyleAlignmentBaseline::Central:
      case StyleAlignmentBaseline::Ideographic:
      case StyleAlignmentBaseline::Mathematical:
      case StyleAlignmentBaseline::Hanging: {
        nscoord parentBaseline = GetFontBaseline(fm, alignmentBaseline) *
                                 lineWM.FlowRelativeToLineRelativeFactor();
        pfd->mBounds.BStart(lineWM) =
            baselineBCoord - parentBaseline - pfd->mAscent;
        // For child span frames, additional adjustment is required to align
        // child's requested font baseline to the parent's font baseline.
        // Non-span child frames already have the requested baseline's offset
        // baked into their ascents, so no additional adjustment is required.
        // See Baseline::SynthesizeBOffsetFromInnerBox.
        if (frameSpan) {
          RefPtr spanFm = nsLayoutUtils::GetInflatedFontMetricsForFrame(frame);
          nscoord selfBaseline = GetFontBaseline(spanFm, alignmentBaseline) *
                                 lineWM.FlowRelativeToLineRelativeFactor();
          pfd->mBounds.BStart(lineWM) += selfBaseline;
        }
        pfd->mBlockDirAlign = VALIGN_OTHER;
        break;
      }

      case StyleAlignmentBaseline::Middle: {
        // Align the midpoint of the frame with 1/2 the parents
        // x-height above the baseline.
        //
        // TODO(bug 2030203): The css-inline-3 definition of `middle` doesn't
        // match this existing CSS2 implementation. Per css-inline-3, this
        // should align the x-middle baselines of the child and parent (like
        // `alphabetic`/etc. above). But CSS2 aligned the midpoint of the child
        // with the x-middle baseline of the parent, which can differ.
        nscoord parentXHeight =
            lineWM.FlowRelativeToLineRelativeFactor() * fm->XHeight();
        if (frameSpan) {
          pfd->mBounds.BStart(lineWM) =
              baselineBCoord - (parentXHeight + pfd->mBounds.BSize(lineWM)) / 2;
        } else {
          pfd->mBounds.BStart(lineWM) = baselineBCoord -
                                        (parentXHeight + logicalBSize) / 2 +
                                        pfd->mMargin.BStart(lineWM);
        }
        pfd->mBlockDirAlign = VALIGN_OTHER;
        break;
      }

      case StyleAlignmentBaseline::TextTop: {
        // The top of the logical box is aligned with the top of
        // the parent element's text.
        // XXX For vertical text we will need a new API to get the logical
        //     max-ascent here
        nscoord parentAscent =
            lineWM.IsLineInverted() ? fm->MaxDescent() : fm->MaxAscent();
        if (frameSpan) {
          pfd->mBounds.BStart(lineWM) = baselineBCoord - parentAscent -
                                        pfd->mBorderPadding.BStart(lineWM) +
                                        frameSpan->mBStartLeading;
        } else {
          pfd->mBounds.BStart(lineWM) =
              baselineBCoord - parentAscent + pfd->mMargin.BStart(lineWM);
        }
        pfd->mBlockDirAlign = VALIGN_OTHER;
        break;
      }

      case StyleAlignmentBaseline::TextBottom: {
        // The bottom of the logical box is aligned with the
        // bottom of the parent elements text.
        nscoord parentDescent =
            lineWM.IsLineInverted() ? fm->MaxAscent() : fm->MaxDescent();
        if (frameSpan) {
          pfd->mBounds.BStart(lineWM) =
              baselineBCoord + parentDescent - pfd->mBounds.BSize(lineWM) +
              pfd->mBorderPadding.BEnd(lineWM) - frameSpan->mBEndLeading;
        } else {
          pfd->mBounds.BStart(lineWM) = baselineBCoord + parentDescent -
                                        pfd->mBounds.BSize(lineWM) -
                                        pfd->mMargin.BEnd(lineWM);
        }
        pfd->mBlockDirAlign = VALIGN_OTHER;
        break;
      }

      case StyleAlignmentBaseline::MozMiddleWithBaseline: {
        // Align the midpoint of the frame with the baseline of the parent.
        if (frameSpan) {
          pfd->mBounds.BStart(lineWM) =
              baselineBCoord - pfd->mBounds.BSize(lineWM) / 2;
        } else {
          pfd->mBounds.BStart(lineWM) =
              baselineBCoord - logicalBSize / 2 + pfd->mMargin.BStart(lineWM);
        }
        pfd->mBlockDirAlign = VALIGN_OTHER;
        break;
      }
    }

    // Shift from the baseline
    if (baselineShiftEnum) {
      switch (*baselineShiftEnum) {
        case StyleBaselineShiftKeyword::Sub:
        case StyleBaselineShiftKeyword::Super:
          pfd->mBounds.BStart(lineWM) +=
              lineWM.FlowRelativeToLineRelativeFactor() *
              (*baselineShiftEnum == StyleBaselineShiftKeyword::Sub
                   ? fm->SubscriptOffset()
                   : -fm->SuperscriptOffset());
          break;

        case StyleBaselineShiftKeyword::Top: {
          pfd->mBlockDirAlign = VALIGN_TOP;
          nscoord subtreeBSize = logicalBSize;
          if (frameSpan) {
            subtreeBSize = frameSpan->mMaxBCoord - frameSpan->mMinBCoord;
            NS_ASSERTION(subtreeBSize >= logicalBSize,
                         "unexpected subtree block size");
          }
          if (subtreeBSize > maxStartBoxBSize) {
            maxStartBoxBSize = subtreeBSize;
          }
          break;
        }

        case StyleBaselineShiftKeyword::Bottom: {
          pfd->mBlockDirAlign = VALIGN_BOTTOM;
          nscoord subtreeBSize = logicalBSize;
          if (frameSpan) {
            subtreeBSize = frameSpan->mMaxBCoord - frameSpan->mMinBCoord;
            NS_ASSERTION(subtreeBSize >= logicalBSize,
                         "unexpected subtree block size");
          }
          if (subtreeBSize > maxEndBoxBSize) {
            maxEndBoxBSize = subtreeBSize;
          }
          break;
        }

        case StyleBaselineShiftKeyword::Center:
          pfd->mBlockDirAlign = VALIGN_CENTER;
          nscoord subtreeBSize = logicalBSize;
          if (frameSpan) {
            subtreeBSize = frameSpan->mMaxBCoord - frameSpan->mMinBCoord;
            NS_ASSERTION(subtreeBSize >= logicalBSize,
                         "unexpected subtree block size");
          }
          if (subtreeBSize > maxStartBoxBSize) {
            maxStartBoxBSize = subtreeBSize;
          }
          if (subtreeBSize > maxEndBoxBSize) {
            maxEndBoxBSize = subtreeBSize;
          }
          break;
      }
    } else {
      // We have either a coord, a percent, or a calc().
      nscoord offset = baselineShift.AsLength().Resolve([&] {
        // Percentages are like lengths, except treated as a percentage
        // of the elements line block size value.
        float inflation =
            GetInflationForBlockDirAlignment(frame, mInflationMinFontSize);
        return ReflowInput::CalcLineHeight(*frame->Style(),
                                           frame->PresContext(),
                                           frame->GetContent(), inflation);
      });

      // According to the CSS2 spec (10.8.1), a positive value
      // "raises" the box by the given distance while a negative value
      // "lowers" the box by the given distance (with zero being the
      // baseline). Since Y coordinates increase towards the bottom of
      // the screen we reverse the sign, unless the line orientation is
      // inverted relative to block direction.
      pfd->mBounds.BStart(lineWM) +=
          -1 * offset * lineWM.FlowRelativeToLineRelativeFactor();
    }

    // Update minBCoord/maxBCoord for frames that we just placed. Do not factor
    // text into the equation.
    if (pfd->mBlockDirAlign == VALIGN_OTHER) {
      // Text frames do not contribute to the min/max Y values for the
      // line (instead their parent frame's font-size contributes).
      // XXXrbs -- relax this restriction because it causes text frames
      //           to jam together when 'font-size-adjust' is enabled
      //           and layout is using dynamic font heights (bug 20394)
      //        -- Note #1: With this code enabled and with the fact that we are
      //           not using Em[Ascent|Descent] as nsDimensions for text
      //           metrics in GFX mean that the discussion in bug 13072 cannot
      //           hold.
      //        -- Note #2: We still don't want empty-text frames to interfere.
      //           For example in quirks mode, avoiding empty text frames
      //           prevents "tall" lines around elements like <hr> since the
      //           rules of <hr> in quirks.css have pseudo text contents with LF
      //           in them.
      bool canUpdate;
      if (pfd->mIsTextFrame) {
        // Only consider text frames if they're not empty and
        // line-height=normal.
        canUpdate = pfd->mIsNonWhitespaceTextFrame &&
                    frame->StyleFont()->mLineHeight.IsNormal();
      } else {
        canUpdate = !pfd->mIsPlaceholder;
      }

      if (canUpdate) {
        nscoord blockStart, blockEnd;
        if (frameSpan) {
          // For spans that were are now placing, use their position
          // plus their already computed min-Y and max-Y values for
          // computing blockStart and blockEnd.
          blockStart = pfd->mBounds.BStart(lineWM) + frameSpan->mMinBCoord;
          blockEnd = pfd->mBounds.BStart(lineWM) + frameSpan->mMaxBCoord;
        } else {
          blockStart =
              pfd->mBounds.BStart(lineWM) - pfd->mMargin.BStart(lineWM);
          blockEnd = blockStart + logicalBSize;
        }
        if (!preMode &&
            mPresContext->CompatibilityMode() != eCompatibility_FullStandards &&
            !logicalBSize) {
          // Check if it's a BR frame that is not alone on its line (it
          // is given a block size of zero to indicate this), and if so reset
          // blockStart and blockEnd so that BR frames don't influence the line.
          if (frame->IsBrFrame()) {
            blockStart = BLOCKDIR_ALIGN_FRAMES_NO_MINIMUM;
            blockEnd = BLOCKDIR_ALIGN_FRAMES_NO_MAXIMUM;
          }
        }
        if (blockStart < minBCoord) {
          minBCoord = blockStart;
        }
        if (blockEnd > maxBCoord) {
          maxBCoord = blockEnd;
        }
#ifdef NOISY_BLOCKDIR_ALIGN
        printf(
            "     [frame]raw: a=%d h=%d bp=%d,%d logical: h=%d leading=%d y=%d "
            "minBCoord=%d maxBCoord=%d\n",
            pfd->mAscent, pfd->mBounds.BSize(lineWM),
            pfd->mBorderPadding.Top(lineWM), pfd->mBorderPadding.Bottom(lineWM),
            logicalBSize, frameSpan ? frameSpan->mBStartLeading : 0,
            pfd->mBounds.BStart(lineWM), minBCoord, maxBCoord);
#endif
      }
      if (psd != mRootSpan) {
        frame->SetRect(lineWM, pfd->mBounds, ContainerSizeForSpan(psd));
      }
    }
    pfd = pfd->mNext;
  }

  // Factor in the minimum line block-size when handling the root-span for
  // the block.
  if (psd == mRootSpan) {
    // We should factor in the block element's minimum line-height (as
    // defined in section 10.8.1 of the css2 spec) assuming that
    // zeroEffectiveSpanBox is not set on the root span.  This only happens
    // in some cases in quirks mode:
    //  (1) if the root span contains non-whitespace text directly (this
    //      is handled by zeroEffectiveSpanBox
    //  (2) if this line has a ::marker
    //  (3) if this is the last line of an LI, DT, or DD element
    //      (The last line before a block also counts, but not before a
    //      BR) (NN4/IE5 quirk)

    // (1) and (2) above
    bool applyMinLH = !zeroEffectiveSpanBox || mHasMarker;
    bool isLastLine =
        !mGotLineBox || (!mLineBox->IsLineWrapped() && !mLineEndsInBR);
    if (!applyMinLH && isLastLine) {
      nsIContent* blockContent = mRootSpan->mFrame->mFrame->GetContent();
      if (blockContent) {
        // (3) above, if the last line of LI, DT, or DD
        if (blockContent->IsAnyOfHTMLElements(nsGkAtoms::li, nsGkAtoms::dt,
                                              nsGkAtoms::dd)) {
          applyMinLH = true;
        }
      }
    }
    if (applyMinLH) {
      if (psd->mHasNonemptyContent ||
          (preMode && ShouldApplyLineHeightInPreserveWhiteSpace(psd)) ||
          mHasMarker) {
#ifdef NOISY_BLOCKDIR_ALIGN
        printf("  [span]==> adjusting min/maxBCoord: currentValues: %d,%d",
               minBCoord, maxBCoord);
#endif
        nscoord minimumLineBSize = mMinLineBSize;
        nscoord blockStart = -nsLayoutUtils::GetCenteredFontBaseline(
            fm, minimumLineBSize, lineWM.IsLineInverted());
        nscoord blockEnd = blockStart + minimumLineBSize;

        if (mStyleText->HasEffectiveTextEmphasis()) {
          nscoord fontMaxHeight =
              mPresContext->NormalizeRubyMetrics()
                  ? mPresContext->RubyPositioningFactor() *
                        (fm->TrimmedAscent() + fm->TrimmedDescent())
                  : fm->MaxHeight();
          nscoord emphasisHeight =
              GetBSizeOfEmphasisMarks(spanFrame, inflation);
          nscoord delta = fontMaxHeight + emphasisHeight - minimumLineBSize;
          if (delta > 0) {
            if (minimumLineBSize < fontMaxHeight) {
              // If the leadings are negative, fill them first.
              nscoord ascent = fm->MaxAscent();
              nscoord descent = fm->MaxDescent();
              if (lineWM.IsLineInverted()) {
                std::swap(ascent, descent);
              }
              blockStart = -ascent;
              blockEnd = descent;
              delta = emphasisHeight;
            }
            LogicalSide side = mStyleText->TextEmphasisSide(
                lineWM, spanFrame->StyleFont()->GetLangAtom());
            if (side == LogicalSide::BStart) {
              blockStart -= delta;
            } else {
              blockEnd += delta;
            }
          }
        }

        minBCoord = std::min(blockStart, minBCoord);
        maxBCoord = std::max(blockEnd, maxBCoord);

#ifdef NOISY_BLOCKDIR_ALIGN
        printf(" new values: %d,%d\n", minBCoord, maxBCoord);
#endif
#ifdef NOISY_BLOCKDIR_ALIGN
        printf(
            "            Used mMinLineBSize: %d, blockStart: %d, blockEnd: "
            "%d\n",
            mMinLineBSize, blockStart, blockEnd);
#endif
      } else {
        // XXX issues:
        // [1] BR's on empty lines stop working
        // [2] May not honor css2's notion of handling empty elements
        // [3] blank lines in a pre-section ("\n") (handled with preMode)

        // XXX Are there other problems with this?
#ifdef NOISY_BLOCKDIR_ALIGN
        printf(
            "  [span]==> zapping min/maxBCoord: currentValues: %d,%d "
            "newValues: 0,0\n",
            minBCoord, maxBCoord);
#endif
        minBCoord = maxBCoord = 0;
      }
    }
  }

  if ((minBCoord == BLOCKDIR_ALIGN_FRAMES_NO_MINIMUM) ||
      (maxBCoord == BLOCKDIR_ALIGN_FRAMES_NO_MAXIMUM)) {
    minBCoord = maxBCoord = baselineBCoord;
  }

  if (psd != mRootSpan && zeroEffectiveSpanBox) {
#ifdef NOISY_BLOCKDIR_ALIGN
    printf("   [span]adjusting for zeroEffectiveSpanBox\n");
    printf(
        "     Original: minBCoord=%d, maxBCoord=%d, bSize=%d, ascent=%d, "
        "logicalBSize=%d, topLeading=%d, bottomLeading=%d\n",
        minBCoord, maxBCoord, spanFramePFD->mBounds.BSize(lineWM),
        spanFramePFD->mAscent, psd->mLogicalBSize, psd->mBStartLeading,
        psd->mBEndLeading);
#endif
    nscoord goodMinBCoord =
        spanFramePFD->mBorderPadding.BStart(lineWM) - psd->mBStartLeading;
    nscoord goodMaxBCoord = goodMinBCoord + psd->mLogicalBSize;

    // For cases like the one in bug 714519 (text-decoration placement
    // or making nsLineLayout::IsZeroBSize() handle
    // vertical-align:top/bottom on a descendant of the line that's not
    // a child of it), we want to treat elements that are
    // vertical-align: top or bottom somewhat like children for the
    // purposes of this quirk.  To some extent, this is guessing, since
    // they might end up being aligned anywhere.  However, we'll guess
    // that they'll be placed aligned with the top or bottom of this
    // frame (as though this frame is the only thing in the line).
    // (Guessing isn't unreasonable, since all we're doing is reducing the
    // scope of a quirk and making the behavior more standards-like.)
    if (maxStartBoxBSize > maxBCoord - minBCoord) {
      // Distribute maxStartBoxBSize to ascent (baselineBCoord - minBCoord), and
      // then to descent (maxBCoord - baselineBCoord) by adjusting minBCoord or
      // maxBCoord, but not to exceed goodMinBCoord and goodMaxBCoord.
      nscoord distribute = maxStartBoxBSize - (maxBCoord - minBCoord);
      nscoord ascentSpace = std::max(minBCoord - goodMinBCoord, 0);
      if (distribute > ascentSpace) {
        distribute -= ascentSpace;
        minBCoord -= ascentSpace;
        nscoord descentSpace = std::max(goodMaxBCoord - maxBCoord, 0);
        maxBCoord += std::min(descentSpace, distribute);
      } else {
        minBCoord -= distribute;
      }
    }
    if (maxEndBoxBSize > maxBCoord - minBCoord) {
      // Likewise, but preferring descent to ascent.
      nscoord distribute = maxEndBoxBSize - (maxBCoord - minBCoord);
      nscoord descentSpace = std::max(goodMaxBCoord - maxBCoord, 0);
      if (distribute > descentSpace) {
        distribute -= descentSpace;
        maxBCoord += descentSpace;
        nscoord ascentSpace = std::max(minBCoord - goodMinBCoord, 0);
        minBCoord -= std::min(ascentSpace, distribute);
      } else {
        maxBCoord += distribute;
      }
    }

    if (minBCoord > goodMinBCoord) {
      nscoord adjust = minBCoord - goodMinBCoord;  // positive

      // shrink the logical extents
      psd->mLogicalBSize -= adjust;
      psd->mBStartLeading -= adjust;
    }
    if (maxBCoord < goodMaxBCoord) {
      nscoord adjust = goodMaxBCoord - maxBCoord;
      psd->mLogicalBSize -= adjust;
      psd->mBEndLeading -= adjust;
    }
    if (minBCoord > 0) {
      // shrink the content by moving its block start down.  This is tricky,
      // since the block start is the 0 for many coordinates, so what we do is
      // move everything else up.
      spanFramePFD->mAscent -= minBCoord;  // move the baseline up
      spanFramePFD->mBounds.BSize(lineWM) -=
          minBCoord;  // move the block end up
      psd->mBStartLeading += minBCoord;
      *psd->mBaseline -= minBCoord;

      pfd = psd->mFirstFrame;
      while (nullptr != pfd) {
        pfd->mBounds.BStart(lineWM) -= minBCoord;  // move all the children
                                                   // back up
        pfd->mFrame->SetRect(lineWM, pfd->mBounds, ContainerSizeForSpan(psd));
        pfd = pfd->mNext;
      }
      maxBCoord -= minBCoord;  // since minBCoord is in the frame's own
                               // coordinate system
      minBCoord = 0;
    }
    if (maxBCoord < spanFramePFD->mBounds.BSize(lineWM)) {
      nscoord adjust = spanFramePFD->mBounds.BSize(lineWM) - maxBCoord;
      spanFramePFD->mBounds.BSize(lineWM) -= adjust;  // move the bottom up
      psd->mBEndLeading += adjust;
    }
#ifdef NOISY_BLOCKDIR_ALIGN
    printf(
        "     New: minBCoord=%d, maxBCoord=%d, bSize=%d, ascent=%d, "
        "logicalBSize=%d, topLeading=%d, bottomLeading=%d\n",
        minBCoord, maxBCoord, spanFramePFD->mBounds.BSize(lineWM),
        spanFramePFD->mAscent, psd->mLogicalBSize, psd->mBStartLeading,
        psd->mBEndLeading);
#endif
  }

  if (psd != mRootSpan) {
    const StyleTextBoxTrim spanTrim = spanFrame->StyleTextReset()->mTextBoxTrim;
    bool shouldApplyTrimStart = bool(spanTrim & StyleTextBoxTrim::TRIM_START);
    bool shouldApplyTrimEnd = bool(spanTrim & StyleTextBoxTrim::TRIM_END);

    // Trims the frame's content box to the resolved text-box-edge metrics.
    if (shouldApplyTrimStart || shouldApplyTrimEnd) {
      // Identify the current total height above and below the baseline,
      // to be compared against the target text-box-edge metrics to determine
      // how much to trim on each side.
      nscoord contentOver =
          spanFramePFD->mAscent - spanFramePFD->mBorderPadding.BStart(lineWM);
      nscoord contentUnder = spanFramePFD->mBounds.BSize(lineWM) -
                             spanFramePFD->mAscent -
                             spanFramePFD->mBorderPadding.BEnd(lineWM);
      const StyleTextBoxEdge& textBoxEdge =
          spanFrame->StyleText()->mTextBoxEdge;
      RefPtr<nsFontMetrics> fm =
          nsLayoutUtils::GetInflatedFontMetricsForFrame(spanFrame);
      const auto [trimmedOver, trimmedUnder] =
          ResolveTextBoxEdgeMetrics(textBoxEdge, fm);

      if (shouldApplyTrimStart) {
        // Trim on the start side by moving the block start coordinate
        // and the frame's ascent up by the trim amount. Additionally
        // move all child frames up by the trim amount.
        const nscoord trimAmount = lineWM.IsLineInverted()
                                       ? contentUnder - trimmedUnder
                                       : contentOver - trimmedOver;
        spanFramePFD->mAscent -= trimAmount;
        spanFramePFD->mBounds.BSize(lineWM) -= trimAmount;
        for (PerFrameData* pfd = psd->mFirstFrame; pfd; pfd = pfd->mNext) {
          pfd->mBounds.BStart(lineWM) -= trimAmount;
          pfd->mFrame->SetRect(lineWM, pfd->mBounds, ContainerSizeForSpan(psd));
        }
        minBCoord -= trimAmount;
        maxBCoord -= trimAmount;
        *psd->mBaseline -= trimAmount;
      }

      if (shouldApplyTrimEnd) {
        // Trim on the end side by reducing the block size of frame.
        const nscoord trimAmount = lineWM.IsLineInverted()
                                       ? contentOver - trimmedOver
                                       : contentUnder - trimmedUnder;
        spanFramePFD->mBounds.BSize(lineWM) -= trimAmount;

        // In vertical-rl, trimming the block end side changes the physical
        // left position, which requires resetting the rects of any children
        // in order to correct their positions.
        if (lineWM.IsVerticalRL()) {
          for (PerFrameData* pfd = psd->mFirstFrame; pfd; pfd = pfd->mNext) {
            pfd->mFrame->SetRect(lineWM, pfd->mBounds,
                                 ContainerSizeForSpan(psd));
          }
        }
      }
    }
  }

  psd->mMinBCoord = minBCoord;
  psd->mMaxBCoord = maxBCoord;
#ifdef NOISY_BLOCKDIR_ALIGN
  printf(
      "  [span]==> minBCoord=%d maxBCoord=%d delta=%d maxStartBoxBSize=%d "
      "maxEndBoxBSize=%d\n",
      minBCoord, maxBCoord, maxBCoord - minBCoord, maxStartBoxBSize,
      maxEndBoxBSize);
#endif
  mMaxStartBoxBSize = std::max(mMaxStartBoxBSize, maxStartBoxBSize);
  mMaxEndBoxBSize = std::max(mMaxEndBoxBSize, maxEndBoxBSize);
}

static void SlideSpanFrameRect(nsIFrame* aFrame, nscoord aDeltaWidth) {
  // This should not use nsIFrame::MovePositionBy because it happens
  // prior to relative positioning.  In particular, because
  // nsBlockFrame::PlaceLine calls aLineLayout.TrimTrailingWhiteSpace()
  // prior to calling aLineLayout.RelativePositionFrames().
  nsPoint p = aFrame->GetPosition();
  p.x -= aDeltaWidth;
  aFrame->SetPosition(p);
}

bool nsLineLayout::TrimTrailingWhiteSpaceIn(PerSpanData* psd,
                                            nscoord* aDeltaISize) {
  PerFrameData* pfd = psd->mFirstFrame;
  if (!pfd) {
    *aDeltaISize = 0;
    return false;
  }
  pfd = pfd->Last();
  while (nullptr != pfd) {
#ifdef REALLY_NOISY_TRIM
    psd->mFrame->mFrame->ListTag(stdout);
    printf(": attempting trim of ");
    pfd->mFrame->ListTag(stdout);
    printf("\n");
#endif
    PerSpanData* childSpan = pfd->mSpan;
    WritingMode lineWM = mRootSpan->mWritingMode;
    if (childSpan) {
      // Maybe the child span has the trailing white-space in it?
      if (TrimTrailingWhiteSpaceIn(childSpan, aDeltaISize)) {
        nscoord deltaISize = *aDeltaISize;
        if (deltaISize) {
          // Adjust the child spans frame size
          pfd->mBounds.ISize(lineWM) -= deltaISize;
          if (psd != mRootSpan) {
            // When the child span is not a direct child of the block
            // we need to update the child spans frame rectangle
            // because it most likely will not be done again. Spans
            // that are direct children of the block will be updated
            // later, however, because the VerticalAlignFrames method
            // will be run after this method.
            nsSize containerSize = ContainerSizeForSpan(childSpan);
            nsIFrame* f = pfd->mFrame;
            LogicalRect r(lineWM, f->GetRect(), containerSize);
            r.ISize(lineWM) -= deltaISize;
            f->SetRect(lineWM, r, containerSize);
          }

          // Adjust the inline end edge of the span that contains the child span
          psd->mICoord -= deltaISize;

          // Slide any frames that follow the child span over by the
          // correct amount. The only thing that can follow the child
          // span is empty stuff, so we are just making things
          // sensible (keeping the combined area honest).
          while (pfd->mNext) {
            pfd = pfd->mNext;
            pfd->mBounds.IStart(lineWM) -= deltaISize;
            if (psd != mRootSpan) {
              // When the child span is not a direct child of the block
              // we need to update the child span's frame rectangle
              // because it most likely will not be done again. Spans
              // that are direct children of the block will be updated
              // later, however, because the VerticalAlignFrames method
              // will be run after this method.
              SlideSpanFrameRect(pfd->mFrame, deltaISize);
            }
          }
        }
        return true;
      }
    } else if (!pfd->mIsTextFrame && !pfd->mSkipWhenTrimmingWhitespace) {
      // If we hit a frame on the end that's not text and not a placeholder,
      // then there is no trailing whitespace to trim. Stop the search.
      *aDeltaISize = 0;
      return true;
    } else if (pfd->mIsTextFrame) {
      // Call TrimTrailingWhiteSpace even on empty textframes because they
      // might have a soft hyphen which should now appear, changing the frame's
      // width
      nsTextFrame::TrimOutput trimOutput =
          static_cast<nsTextFrame*>(pfd->mFrame)
              ->TrimTrailingWhiteSpace(
                  mLineContainerRI.mRenderingContext->GetDrawTarget());
#ifdef NOISY_TRIM
      psd->mFrame->mFrame->ListTag(stdout);
      printf(": trim of ");
      pfd->mFrame->ListTag(stdout);
      printf(" returned %d\n", trimOutput.mDeltaWidth);
#endif

      if (trimOutput.mChanged) {
        pfd->mRecomputeOverflow = true;
      }

      // Delta width not being zero means that
      // there is trimmed space in the frame.
      if (trimOutput.mDeltaWidth) {
        pfd->mBounds.ISize(lineWM) -= trimOutput.mDeltaWidth;

        // If any trailing space is trimmed, the justification opportunity
        // generated by the space should be removed as well.
        pfd->mJustificationInfo.CancelOpportunityForTrimmedSpace();

        // See if the text frame has already been placed in its parent
        if (psd != mRootSpan) {
          // The frame was already placed during psd's
          // reflow. Update the frames rectangle now.
          pfd->mFrame->SetRect(lineWM, pfd->mBounds, ContainerSizeForSpan(psd));
        }

        // Adjust containing span's right edge
        psd->mICoord -= trimOutput.mDeltaWidth;

        // Slide any frames that follow the text frame over by the
        // right amount. The only thing that can follow the text
        // frame is empty stuff, so we are just making things
        // sensible (keeping the combined area honest).
        while (pfd->mNext) {
          pfd = pfd->mNext;
          pfd->mBounds.IStart(lineWM) -= trimOutput.mDeltaWidth;
          if (psd != mRootSpan) {
            // When the child span is not a direct child of the block
            // we need to update the child spans frame rectangle
            // because it most likely will not be done again. Spans
            // that are direct children of the block will be updated
            // later, however, because the VerticalAlignFrames method
            // will be run after this method.
            SlideSpanFrameRect(pfd->mFrame, trimOutput.mDeltaWidth);
          }
        }
      }

      if (pfd->mIsNonEmptyTextFrame || trimOutput.mChanged) {
        // Pass up to caller so they can shrink their span
        *aDeltaISize = trimOutput.mDeltaWidth;
        return true;
      }
    }
    pfd = pfd->mPrev;
  }

  *aDeltaISize = 0;
  return false;
}

bool nsLineLayout::TrimTrailingWhiteSpace() {
  nscoord deltaISize;
  TrimTrailingWhiteSpaceIn(mRootSpan, &deltaISize);
  return 0 != deltaISize;
}

bool nsLineLayout::PerFrameData::ParticipatesInJustification() const {
  if (mIsMarker || mIsEmpty || mSkipWhenTrimmingWhitespace) {
    // Skip ::markers, empty frames, and placeholders
    return false;
  }
  if (mIsTextFrame && !mIsNonWhitespaceTextFrame &&
      static_cast<nsTextFrame*>(mFrame)->IsAtEndOfLine()) {
    // Skip trimmed whitespaces
    return false;
  }
  return true;
}

struct nsLineLayout::JustificationComputationState {
  PerFrameData* mFirstParticipant;
  PerFrameData* mLastParticipant;
  // When we are going across a boundary of ruby base, i.e. entering
  // one, leaving one, or both, the following fields will be set to
  // the corresponding ruby base frame for handling ruby-align.
  PerFrameData* mLastExitedRubyBase;
  PerFrameData* mLastEnteredRubyBase;

  JustificationComputationState()
      : mFirstParticipant(nullptr),
        mLastParticipant(nullptr),
        mLastExitedRubyBase(nullptr),
        mLastEnteredRubyBase(nullptr) {}
};

static bool IsRubyAlignSpaceAround(nsIFrame* aRubyBase) {
  return aRubyBase->StyleText()->mRubyAlign == StyleRubyAlign::SpaceAround;
}

/**
 * Assign justification gaps for justification
 * opportunities across two frames.
 */
/* static */
int nsLineLayout::AssignInterframeJustificationGaps(
    PerFrameData* aFrame, JustificationComputationState& aState) {
  PerFrameData* prev = aState.mLastParticipant;
  MOZ_ASSERT(prev);

  auto& assign = aFrame->mJustificationAssignment;
  auto& prevAssign = prev->mJustificationAssignment;

  if (aState.mLastExitedRubyBase || aState.mLastEnteredRubyBase) {
    PerFrameData* exitedRubyBase = aState.mLastExitedRubyBase;
    if (!exitedRubyBase || IsRubyAlignSpaceAround(exitedRubyBase->mFrame)) {
      prevAssign.mGapsAtEnd = 1;
    } else {
      exitedRubyBase->mJustificationAssignment.mGapsAtEnd = 1;
    }

    PerFrameData* enteredRubyBase = aState.mLastEnteredRubyBase;
    if (!enteredRubyBase || IsRubyAlignSpaceAround(enteredRubyBase->mFrame)) {
      assign.mGapsAtStart = 1;
    } else {
      enteredRubyBase->mJustificationAssignment.mGapsAtStart = 1;
    }

    // We are no longer going across a ruby base boundary.
    aState.mLastExitedRubyBase = nullptr;
    aState.mLastEnteredRubyBase = nullptr;
    return 1;
  }

  const auto& info = aFrame->mJustificationInfo;
  const auto& prevInfo = prev->mJustificationInfo;
  if (!info.mIsStartJustifiable && !prevInfo.mIsEndJustifiable) {
    return 0;
  }

  if (!info.mIsStartJustifiable) {
    prevAssign.mGapsAtEnd = 2;
    assign.mGapsAtStart = 0;
  } else if (!prevInfo.mIsEndJustifiable) {
    prevAssign.mGapsAtEnd = 0;
    assign.mGapsAtStart = 2;
  } else {
    prevAssign.mGapsAtEnd = 1;
    assign.mGapsAtStart = 1;
  }
  return 1;
}

/**
 * Compute the justification info of the given span, and store the
 * number of inner opportunities into the frame's justification info.
 * It returns the number of non-inner opportunities it detects.
 */
int32_t nsLineLayout::ComputeFrameJustification(
    PerSpanData* aPSD, JustificationComputationState& aState) {
  NS_ASSERTION(aPSD, "null arg");
  NS_ASSERTION(!aState.mLastParticipant || !aState.mLastParticipant->mSpan,
               "Last participant shall always be a leaf frame");
  bool firstChild = true;
  int32_t& innerOpportunities =
      aPSD->mFrame->mJustificationInfo.mInnerOpportunities;
  MOZ_ASSERT(innerOpportunities == 0,
             "Justification info should not have been set yet.");
  int32_t outerOpportunities = 0;

  for (PerFrameData* pfd = aPSD->mFirstFrame; pfd; pfd = pfd->mNext) {
    if (!pfd->ParticipatesInJustification()) {
      continue;
    }

    bool isRubyBase = pfd->mFrame->IsRubyBaseFrame();
    PerFrameData* outerRubyBase = aState.mLastEnteredRubyBase;
    if (isRubyBase) {
      aState.mLastEnteredRubyBase = pfd;
    }

    int extraOpportunities = 0;
    if (pfd->mSpan) {
      PerSpanData* span = pfd->mSpan;
      extraOpportunities = ComputeFrameJustification(span, aState);
      innerOpportunities += pfd->mJustificationInfo.mInnerOpportunities;
    } else {
      if (pfd->mIsTextFrame) {
        innerOpportunities += pfd->mJustificationInfo.mInnerOpportunities;
      }

      if (!aState.mLastParticipant) {
        aState.mFirstParticipant = pfd;
        // It is not an empty ruby base, but we are not assigning gaps
        // to the content for now. Clear the last entered ruby base so
        // that we can correctly set the last exited ruby base.
        aState.mLastEnteredRubyBase = nullptr;
      } else {
        extraOpportunities = AssignInterframeJustificationGaps(pfd, aState);
      }

      aState.mLastParticipant = pfd;
    }

    if (isRubyBase) {
      if (aState.mLastEnteredRubyBase == pfd) {
        // There is no justification participant inside this ruby base.
        // Ignore this ruby base completely and restore the outer ruby
        // base here.
        aState.mLastEnteredRubyBase = outerRubyBase;
      } else {
        aState.mLastExitedRubyBase = pfd;
      }
    }

    if (firstChild) {
      outerOpportunities = extraOpportunities;
      firstChild = false;
    } else {
      innerOpportunities += extraOpportunities;
    }
  }

  return outerOpportunities;
}

void nsLineLayout::AdvanceAnnotationInlineBounds(PerFrameData* aPFD,
                                                 const nsSize& aContainerSize,
                                                 nscoord aDeltaICoord,
                                                 nscoord aDeltaISize) {
  nsIFrame* frame = aPFD->mFrame;
  LayoutFrameType frameType = frame->Type();
  MOZ_ASSERT(frameType == LayoutFrameType::RubyText ||
             frameType == LayoutFrameType::RubyTextContainer);
  MOZ_ASSERT(aPFD->mSpan, "rt and rtc should have span.");

  PerSpanData* psd = aPFD->mSpan;
  WritingMode lineWM = mRootSpan->mWritingMode;
  aPFD->mBounds.IStart(lineWM) += aDeltaICoord;

  // Check whether this expansion should be counted into the reserved
  // isize or not. When it is a ruby text container, and it has some
  // children linked to the base, it must not have reserved isize,
  // or its children won't align with their bases.  Otherwise, this
  // expansion should be reserved.  There are two cases a ruby text
  // container does not have children linked to the base:
  // 1. it is a container for span; 2. its children are collapsed.
  // See bug 1055674 for the second case.
  if (frameType == LayoutFrameType::RubyText ||
      // This ruby text container is a span.
      (psd->mFirstFrame == psd->mLastFrame && psd->mFirstFrame &&
       !psd->mFirstFrame->mIsLinkedToBase)) {
    // For ruby text frames, only increase frames
    // which are not auto-hidden.
    if (frameType != LayoutFrameType::RubyText ||
        !static_cast<nsRubyTextFrame*>(frame)->IsCollapsed()) {
      nscoord reservedISize = RubyUtils::GetReservedISize(frame);
      RubyUtils::SetReservedISize(frame, reservedISize + aDeltaISize);
    }
  } else {
    // It is a normal ruby text container. Its children will expand
    // themselves properly. We only need to expand its own size here.
    aPFD->mBounds.ISize(lineWM) += aDeltaISize;
  }
  aPFD->mFrame->SetRect(lineWM, aPFD->mBounds, aContainerSize);
}

/**
 * This function applies the changes of icoord and isize caused by
 * justification to annotations of the given frame.
 */
void nsLineLayout::ApplyLineJustificationToAnnotations(PerFrameData* aPFD,
                                                       nscoord aDeltaICoord,
                                                       nscoord aDeltaISize) {
  for (auto* pfd = aPFD->mNextAnnotation; pfd; pfd = pfd->mNextAnnotation) {
    nsSize containerSize = pfd->mFrame->GetParent()->GetSize();
    AdvanceAnnotationInlineBounds(pfd, containerSize, aDeltaICoord,
                                  aDeltaISize);

    // There are two cases where an annotation frame has siblings which
    // do not attached to a ruby base-level frame:
    // 1. there's an intra-annotation whitespace which has no intra-base
    //    white-space to pair with;
    // 2. there are not enough ruby bases to be paired with annotations.
    // In these cases, their size should not be affected, but we still
    // need to move them so that they won't overlap other frames.
    for (auto* sibling = pfd->mNext; sibling && !sibling->mIsLinkedToBase;
         sibling = sibling->mNext) {
      AdvanceAnnotationInlineBounds(sibling, containerSize,
                                    aDeltaICoord + aDeltaISize, 0);
    }
  }
}

nscoord nsLineLayout::ApplyFrameJustification(
    PerSpanData* aPSD, JustificationApplicationState& aState) {
  NS_ASSERTION(aPSD, "null arg");

  nscoord deltaICoord = 0;
  for (PerFrameData* pfd = aPSD->mFirstFrame; pfd; pfd = pfd->mNext) {
    nscoord dw = 0;
    WritingMode lineWM = mRootSpan->mWritingMode;
    const auto& assign = pfd->mJustificationAssignment;
    bool isInlineText =
        pfd->mIsTextFrame && !pfd->mWritingMode.IsOrthogonalTo(lineWM);

    // Don't apply justification if the frame doesn't participate. Same
    // as the condition used in ComputeFrameJustification. Note that,
    // we still need to move the frame based on deltaICoord even if the
    // frame itself doesn't expand.
    if (pfd->ParticipatesInJustification()) {
      if (isInlineText) {
        if (aState.IsJustifiable()) {
          // Set corresponding justification gaps here, so that the
          // text frame knows how it should add gaps at its sides.
          const auto& info = pfd->mJustificationInfo;
          auto textFrame = static_cast<nsTextFrame*>(pfd->mFrame);
          textFrame->AssignJustificationGaps(assign);
          dw = aState.Consume(JustificationUtils::CountGaps(info, assign));
        }

        if (dw) {
          pfd->mRecomputeOverflow = true;
        }
      } else {
        if (nullptr != pfd->mSpan) {
          dw = ApplyFrameJustification(pfd->mSpan, aState);
        }
      }
    } else {
      MOZ_ASSERT(!assign.TotalGaps(),
                 "Non-participants shouldn't have assigned gaps");
    }

    pfd->mBounds.ISize(lineWM) += dw;
    nscoord gapsAtEnd = 0;
    if (!isInlineText && assign.TotalGaps()) {
      // It is possible that we assign gaps to non-text frame or an
      // orthogonal text frame. Apply the gaps as margin for them.
      deltaICoord += aState.Consume(assign.mGapsAtStart);
      gapsAtEnd = aState.Consume(assign.mGapsAtEnd);
      dw += gapsAtEnd;
    }
    pfd->mBounds.IStart(lineWM) += deltaICoord;

    // The gaps added to the end of the frame should also be
    // excluded from the isize added to the annotation.
    ApplyLineJustificationToAnnotations(pfd, deltaICoord, dw - gapsAtEnd);
    deltaICoord += dw;
    pfd->mFrame->SetRect(lineWM, pfd->mBounds, ContainerSizeForSpan(aPSD));
  }
  return deltaICoord;
}

static nsIFrame* FindNearestRubyBaseAncestor(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame->Style()->ShouldSuppressLineBreak());
  while (aFrame && !aFrame->IsRubyBaseFrame()) {
    aFrame = aFrame->GetParent();
  }
  // XXX It is possible that no ruby base ancestor is found because of
  // some edge cases like form control or canvas inside ruby text.
  // See bug 1138092 comment 4.
  NS_WARNING_ASSERTION(aFrame, "no ruby base ancestor?");
  return aFrame;
}

/**
 * This method expands the given frame by the given reserved isize.
 */
void nsLineLayout::ExpandRubyBox(PerFrameData* aFrame, nscoord aReservedISize,
                                 const nsSize& aContainerSize) {
  WritingMode lineWM = mRootSpan->mWritingMode;
  auto rubyAlign = aFrame->mFrame->StyleText()->mRubyAlign;
  switch (rubyAlign) {
    case StyleRubyAlign::Start:
      // do nothing for start
      break;
    case StyleRubyAlign::SpaceBetween:
    case StyleRubyAlign::SpaceAround: {
      int32_t opportunities = aFrame->mJustificationInfo.mInnerOpportunities;
      int32_t gaps = opportunities * 2;
      if (rubyAlign == StyleRubyAlign::SpaceAround) {
        // Each expandable ruby box with ruby-align space-around has a
        // gap at each of its sides. For rb/rbc, see comment in
        // AssignInterframeJustificationGaps; for rt/rtc, see comment
        // in ExpandRubyBoxWithAnnotations.
        gaps += 2;
      }
      if (gaps > 0) {
        JustificationApplicationState state(gaps, aReservedISize);
        ApplyFrameJustification(aFrame->mSpan, state);
        break;
      }
      // If there are no justification opportunities for space-between,
      // fall-through to center per spec.
      [[fallthrough]];
    }
    case StyleRubyAlign::Center:
      // Indent all children by half of the reserved inline size.
      for (PerFrameData* child = aFrame->mSpan->mFirstFrame; child;
           child = child->mNext) {
        child->mBounds.IStart(lineWM) += aReservedISize / 2;
        child->mFrame->SetRect(lineWM, child->mBounds, aContainerSize);
      }
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown ruby-align value");
  }

  aFrame->mBounds.ISize(lineWM) += aReservedISize;
  aFrame->mFrame->SetRect(lineWM, aFrame->mBounds, aContainerSize);
}

/**
 * This method expands the given frame by the reserved inline size.
 * It also expands its annotations if they are expandable and have
 * reserved isize larger than zero.
 */
void nsLineLayout::ExpandRubyBoxWithAnnotations(PerFrameData* aFrame,
                                                const nsSize& aContainerSize) {
  nscoord reservedISize = RubyUtils::GetReservedISize(aFrame->mFrame);
  if (reservedISize) {
    ExpandRubyBox(aFrame, reservedISize, aContainerSize);
  }

  WritingMode lineWM = mRootSpan->mWritingMode;
  bool isLevelContainer = aFrame->mFrame->IsRubyBaseContainerFrame();
  for (PerFrameData* annotation = aFrame->mNextAnnotation; annotation;
       annotation = annotation->mNextAnnotation) {
    if (lineWM.IsOrthogonalTo(annotation->mFrame->GetWritingMode())) {
      // Inter-character case: don't attempt to expand ruby annotations.
      continue;
    }
    if (isLevelContainer) {
      nsIFrame* rtcFrame = annotation->mFrame;
      MOZ_ASSERT(rtcFrame->IsRubyTextContainerFrame());
      // It is necessary to set the rect again because the container
      // width was unknown, and zero was used instead when we reflow
      // them. The corresponding base containers were repositioned in
      // VerticalAlignFrames and PlaceTopBottomCenterFrames.
      MOZ_ASSERT(rtcFrame->GetLogicalSize(lineWM) ==
                 annotation->mBounds.Size(lineWM));
      rtcFrame->SetPosition(lineWM, annotation->mBounds.Origin(lineWM),
                            aContainerSize);
    }

    nscoord reservedISize = RubyUtils::GetReservedISize(annotation->mFrame);
    if (!reservedISize) {
      continue;
    }

    MOZ_ASSERT(annotation->mSpan);
    JustificationComputationState computeState;
    ComputeFrameJustification(annotation->mSpan, computeState);
    if (!computeState.mFirstParticipant) {
      continue;
    }
    if (IsRubyAlignSpaceAround(annotation->mFrame)) {
      // Add one gap at each side of this annotation.
      computeState.mFirstParticipant->mJustificationAssignment.mGapsAtStart = 1;
      computeState.mLastParticipant->mJustificationAssignment.mGapsAtEnd = 1;
    }
    nsIFrame* parentFrame = annotation->mFrame->GetParent();
    nsSize containerSize = parentFrame->GetSize();
    MOZ_ASSERT(containerSize == aContainerSize ||
                   parentFrame->IsRubyTextContainerFrame(),
               "Container width should only be different when the current "
               "annotation is a ruby text frame, whose parent is not same "
               "as its base frame.");
    ExpandRubyBox(annotation, reservedISize, containerSize);
    ExpandInlineRubyBoxes(annotation->mSpan);
  }
}

/**
 * This method looks for all expandable ruby box in the given span, and
 * calls ExpandRubyBox to expand them in depth-first preorder.
 */
void nsLineLayout::ExpandInlineRubyBoxes(PerSpanData* aSpan) {
  nsSize containerSize = ContainerSizeForSpan(aSpan);
  for (PerFrameData* pfd = aSpan->mFirstFrame; pfd; pfd = pfd->mNext) {
    if (RubyUtils::IsExpandableRubyBox(pfd->mFrame)) {
      ExpandRubyBoxWithAnnotations(pfd, containerSize);
    }
    if (pfd->mSpan) {
      ExpandInlineRubyBoxes(pfd->mSpan);
    }
  }
}

nscoord nsLineLayout::GetHangFrom(const PerSpanData* aSpan,
                                  bool aLineIsRTL) const {
  const PerFrameData* pfd = aSpan->mLastFrame;
  nscoord result = 0;
  while (pfd) {
    if (const PerSpanData* childSpan = pfd->mSpan) {
      return GetHangFrom(childSpan, aLineIsRTL);
    }
    if (pfd->mIsTextFrame) {
      auto* lastText = static_cast<nsTextFrame*>(pfd->mFrame);
      result = lastText->GetHangableISize();
      if (result) {
        // If the hangable space will be at the start edge of the line, due to
        // its bidi direction being against the line direction, we flag this by
        // negating the advance.
        lastText->EnsureTextRun(nsTextFrame::eInflated);
        auto* textRun = lastText->GetTextRun(nsTextFrame::eInflated);
        if (textRun && textRun->IsRightToLeft() != aLineIsRTL) {
          result = -result;
        }
      }
      return result;
    }
    if (!pfd->mSkipWhenTrimmingWhitespace) {
      // If we hit a frame on the end that's not text and not a placeholder or
      // <br>, then there is no trailing whitespace to hang. Stop the search.
      return result;
    }
    // Scan back for a preceding frame whose whitespace we can hang.
    pfd = pfd->mPrev;
  }
  return result;
}

gfxTextRun::TrimmableWS nsLineLayout::GetTrimFrom(const PerSpanData* aSpan,
                                                  bool aLineIsRTL) const {
  const PerFrameData* pfd = aSpan->mLastFrame;
  while (pfd) {
    if (const PerSpanData* childSpan = pfd->mSpan) {
      return GetTrimFrom(childSpan, aLineIsRTL);
    }
    if (pfd->mIsTextFrame) {
      auto* lastText = static_cast<nsTextFrame*>(pfd->mFrame);
      auto result = lastText->GetTrimmableWS();
      if (result.mAdvance) {
        lastText->EnsureTextRun(nsTextFrame::eInflated);
        auto* textRun = lastText->GetTextRun(nsTextFrame::eInflated);
        if (textRun && textRun->IsRightToLeft() != aLineIsRTL) {
          result.mAdvance = -result.mAdvance;
        }
      }
      return result;
    }
    if (!pfd->mSkipWhenTrimmingWhitespace) {
      // If we hit a frame on the end that's not text and not a placeholder or
      // <br>, then there is no trailing whitespace to trim. Stop the search.
      return gfxTextRun::TrimmableWS{};
    }
    // Scan back for a preceding frame whose whitespace we can trim.
    pfd = pfd->mPrev;
  }
  return gfxTextRun::TrimmableWS{};
}

// Align inline frames within the line according to the CSS text-align
// property.
void nsLineLayout::TextAlignLine(nsLineBox* aLine, bool aIsLastLine) {
  /**
   * NOTE: aIsLastLine ain't necessarily so: it is correctly set by caller
   * only in cases where the last line needs special handling.
   */
  PerSpanData* psd = mRootSpan;
  WritingMode lineWM = psd->mWritingMode;
  LAYOUT_WARN_IF_FALSE(psd->mIEnd != NS_UNCONSTRAINEDSIZE,
                       "have unconstrained width; this should only result from "
                       "very large sizes, not attempts at intrinsic width "
                       "calculation");
  nscoord availISize = psd->mIEnd - psd->mIStart;
  nscoord remainingISize = availISize - aLine->ISize();
#ifdef NOISY_INLINEDIR_ALIGN
  LineContainerFrame()->ListTag(stdout);
  printf(": availISize=%d lineBounds.IStart=%d lineISize=%d delta=%d\n",
         availISize, aLine->IStart(), aLine->ISize(), remainingISize);
#endif

  nscoord dx = 0;
  StyleTextAlign textAlign =
      aIsLastLine ? mStyleText->TextAlignForLastLine() : mStyleText->mTextAlign;

  // Check if there's trailing whitespace we need to "hang" at line-wrap.
  nscoord hang = 0;
  uint32_t trimCount = 0;
  if (aLine->IsLineWrapped()) {
    if (textAlign == StyleTextAlign::Justify) {
      auto trim = GetTrimFrom(mRootSpan, lineWM.IsBidiRTL());
      hang = NSToCoordRound(trim.mAdvance);
      trimCount = trim.mCount;
    } else {
      hang = GetHangFrom(mRootSpan, lineWM.IsBidiRTL());
    }
  }

  bool isSVG = LineContainerFrame()->IsInSVGTextSubtree();
  bool doTextAlign = remainingISize > 0 || hang != 0;

  int32_t additionalGaps = 0;
  if (!isSVG &&
      (mHasRuby || (doTextAlign && textAlign == StyleTextAlign::Justify))) {
    JustificationComputationState computeState;
    ComputeFrameJustification(psd, computeState);
    if (mHasRuby && computeState.mFirstParticipant) {
      PerFrameData* firstFrame = computeState.mFirstParticipant;
      if (firstFrame->mFrame->Style()->ShouldSuppressLineBreak()) {
        MOZ_ASSERT(!firstFrame->mJustificationAssignment.mGapsAtStart);
        nsIFrame* rubyBase = FindNearestRubyBaseAncestor(firstFrame->mFrame);
        if (rubyBase && IsRubyAlignSpaceAround(rubyBase)) {
          firstFrame->mJustificationAssignment.mGapsAtStart = 1;
          additionalGaps++;
        }
      }
      PerFrameData* lastFrame = computeState.mLastParticipant;
      if (lastFrame->mFrame->Style()->ShouldSuppressLineBreak()) {
        MOZ_ASSERT(!lastFrame->mJustificationAssignment.mGapsAtEnd);
        nsIFrame* rubyBase = FindNearestRubyBaseAncestor(lastFrame->mFrame);
        if (rubyBase && IsRubyAlignSpaceAround(rubyBase)) {
          lastFrame->mJustificationAssignment.mGapsAtEnd = 1;
          additionalGaps++;
        }
      }
    }
  }

  if (!isSVG && doTextAlign) {
    switch (textAlign) {
      case StyleTextAlign::Justify: {
        int32_t opportunities =
            psd->mFrame->mJustificationInfo.mInnerOpportunities -
            (hang ? trimCount : 0);
        if (opportunities > 0) {
          int32_t gaps = opportunities * 2 + additionalGaps;
          remainingISize += std::abs(hang);
          JustificationApplicationState applyState(gaps, remainingISize);

          // Apply the justification, and make sure to update our linebox
          // width to account for it.
          aLine->ExpandBy(ApplyFrameJustification(psd, applyState),
                          ContainerSizeForSpan(psd));

          // If the trimmable trailing whitespace that we want to hang had
          // reverse-inline directionality, adjust line position to account for
          // it being at the inline-start side.
          // On top of the original "hang" amount, justification will have
          // modified its width, so we include that adjustment here.
          if (hang < 0) {
            dx = hang - trimCount * remainingISize / opportunities;
          }

          // Gaps that belong to trimmed whitespace were not included in the
          // applyState count, so we need to add them here for the assert.
          DebugOnly<int32_t> trimmedGaps = hang ? trimCount * 2 : 0;
          MOZ_ASSERT(applyState.mGaps.mHandled ==
                         applyState.mGaps.mCount + trimmedGaps,
                     "Unprocessed justification gaps");
          // Similarly, account for the adjustment applied to the trimmed
          // whitespace, which is in addition to the adjustment that applies
          // within the actual width of the line.
          DebugOnly<int32_t> trimmedAdjustment =
              trimCount * remainingISize / opportunities;
          NS_ASSERTION(applyState.mWidth.mConsumed ==
                           applyState.mWidth.mAvailable + trimmedAdjustment,
                       "Unprocessed justification width");
          break;
        }
        // Fall through to the default case if we could not justify to fill
        // the space.
        [[fallthrough]];
      }

      case StyleTextAlign::Start:
        // Default alignment is to start edge so do nothing, except to apply
        // any "reverse-hang" amount resulting from reversed-direction trailing
        // space.
        // Char is for tables so treat as start if we find it in block layout.
        if (hang < 0) {
          dx = hang;
        }
        break;

      case StyleTextAlign::Left:
      case StyleTextAlign::MozLeft:
        if (lineWM.IsBidiRTL()) {
          dx = remainingISize + (hang > 0 ? hang : 0);
        } else if (hang < 0) {
          dx = hang;
        }
        break;

      case StyleTextAlign::Right:
      case StyleTextAlign::MozRight:
        if (lineWM.IsBidiLTR()) {
          dx = remainingISize + (hang > 0 ? hang : 0);
        } else if (hang < 0) {
          dx = hang;
        }
        break;

      case StyleTextAlign::End:
        dx = remainingISize + (hang > 0 ? hang : 0);
        break;

      case StyleTextAlign::Center:
      case StyleTextAlign::MozCenter:
        dx = (remainingISize + hang) / 2;
        break;
    }
  }

  if (mHasRuby) {
    ExpandInlineRubyBoxes(mRootSpan);
  }

  PerFrameData* startFrame = psd->mFirstFrame;
  MOZ_ASSERT(startFrame, "empty line?");
  if (startFrame->mIsMarker) {
    // ::marker shouldn't participate in bidi reordering nor text alignment.
    startFrame = startFrame->mNext;
    MOZ_ASSERT(startFrame, "no frame after ::marker?");
    MOZ_ASSERT(!startFrame->mIsMarker, "multiple ::markers?");
  }

  const bool bidi = mPresContext->BidiEnabled() &&
                    (!mPresContext->IsVisualMode() || lineWM.IsBidiRTL());
  if (bidi) {
    nsBidiPresUtils::ReorderFrames(startFrame->mFrame, aLine->GetChildCount(),
                                   lineWM, mContainerSize,
                                   psd->mIStart + mTextIndent + dx);
  }

  if (dx) {
    // For the bidi case, if startFrame is a ::first-line frame, the mIStart and
    // mTextIndent offsets will already have been applied to its position, but
    // we still need to apply the text-align adjustment |dx| to its position.
    const bool needToAdjustFrames = !bidi || startFrame->mFrame->IsLineFrame();
    MOZ_ASSERT_IF(startFrame->mFrame->IsLineFrame(), !startFrame->mNext);
    if (needToAdjustFrames) {
      for (PerFrameData* pfd = startFrame; pfd; pfd = pfd->mNext) {
        pfd->mBounds.IStart(lineWM) += dx;
        pfd->mFrame->SetRect(lineWM, pfd->mBounds, ContainerSizeForSpan(psd));
      }
    }
    aLine->IndentBy(dx, ContainerSize());
  }
}

// This method applies any relative positioning to the given frame.
void nsLineLayout::ApplyRelativePositioning(PerFrameData* aPFD) {
  if (!aPFD->mIsRelativelyOrStickyPos) {
    return;
  }

  nsIFrame* frame = aPFD->mFrame;
  WritingMode frameWM = aPFD->mWritingMode;
  LogicalPoint origin = frame->GetLogicalPosition(ContainerSize());
  // right and bottom are handled by
  // ReflowInput::ComputeRelativeOffsets
  ReflowInput::ApplyRelativePositioning(frame, frameWM, aPFD->mOffsets, &origin,
                                        ContainerSize());
  frame->SetPosition(frameWM, origin, ContainerSize());
}

// This method do relative positioning for ruby annotations.
void nsLineLayout::RelativePositionAnnotations(PerSpanData* aRubyPSD,
                                               OverflowAreas& aOverflowAreas) {
  MOZ_ASSERT(aRubyPSD->mFrame->mFrame->IsRubyFrame());
  for (PerFrameData* pfd = aRubyPSD->mFirstFrame; pfd; pfd = pfd->mNext) {
    MOZ_ASSERT(pfd->mFrame->IsRubyBaseContainerFrame());
    for (PerFrameData* rtc = pfd->mNextAnnotation; rtc;
         rtc = rtc->mNextAnnotation) {
      nsIFrame* rtcFrame = rtc->mFrame;
      MOZ_ASSERT(rtcFrame->IsRubyTextContainerFrame());
      ApplyRelativePositioning(rtc);
      OverflowAreas rtcOverflowAreas;
      RelativePositionFrames(rtc->mSpan, rtcOverflowAreas);
      aOverflowAreas.UnionWith(rtcOverflowAreas + rtcFrame->GetPosition());
    }
  }
}

void nsLineLayout::RelativePositionFrames(PerSpanData* psd,
                                          OverflowAreas& aOverflowAreas) {
  OverflowAreas overflowAreas;
  WritingMode wm = psd->mWritingMode;
  if (psd != mRootSpan) {
    // The span's overflow areas come in three parts:
    // -- this frame's width and height
    // -- pfd->mOverflowAreas, which is the area of a ::marker or the union
    // of a relatively positioned frame's absolute children
    // -- the bounds of all inline descendants
    // The former two parts are computed right here, we gather the descendants
    // below.
    // At this point psd->mFrame->mBounds might be out of date since
    // bidi reordering can move and resize the frames. So use the frame's
    // rect instead of mBounds.
    nsRect adjustedBounds(nsPoint(0, 0), psd->mFrame->mFrame->GetSize());

    overflowAreas.ScrollableOverflow().UnionRect(
        psd->mFrame->mOverflowAreas.ScrollableOverflow(), adjustedBounds);
    overflowAreas.InkOverflow().UnionRect(
        psd->mFrame->mOverflowAreas.InkOverflow(), adjustedBounds);
  } else {
    // Note(dshin, bug 1940938): `mICoord` can be negative due to a negative
    // `text-indent` value (i.e. "Outdenting"). Ensure that we have a valid
    // overflow rect for that case.
    const auto iStart = std::min(psd->mIStart, psd->mICoord);
    const auto iSize = std::abs(psd->mICoord - psd->mIStart);
    LogicalRect rect(wm, iStart, mBStartEdge, iSize, mFinalLineBSize);
    // The minimum combined area for the frames that are direct
    // children of the block starts at the upper left corner of the
    // line and is sized to match the size of the line's bounding box
    // (the same size as the values returned from VerticalAlignFrames)
    overflowAreas.InkOverflow() = rect.GetPhysicalRect(wm, ContainerSize());
    overflowAreas.ScrollableOverflow() = overflowAreas.InkOverflow();
  }

  for (PerFrameData* pfd = psd->mFirstFrame; pfd; pfd = pfd->mNext) {
    nsIFrame* frame = pfd->mFrame;

    // Adjust the origin of the frame
    ApplyRelativePositioning(pfd);

    // Note: the combined area of a child is in its coordinate
    // system. We adjust the childs combined area into our coordinate
    // system before computing the aggregated value by adding in
    // <b>x</b> and <b>y</b> which were computed above.
    OverflowAreas r;
    if (pfd->mSpan) {
      // Compute a new combined area for the child span before
      // aggregating it into our combined area.
      RelativePositionFrames(pfd->mSpan, r);
    } else {
      r = pfd->mOverflowAreas;
      if (pfd->mIsTextFrame) {
        // We need to recompute overflow areas in four cases:
        // (1) When PFD_RECOMPUTEOVERFLOW is set due to trimming
        // (2) When there are text decorations, since we can't recompute the
        //     overflow area until Reflow and VerticalAlignLine have finished
        // (3) When there are text emphasis marks, since the marks may be
        //     put further away if the text is inside ruby.
        // (4) When there are text strokes
        if (pfd->mRecomputeOverflow ||
            frame->Style()->HasTextDecorationLines() ||
            frame->StyleText()->HasEffectiveTextEmphasis() ||
            frame->StyleText()->HasWebkitTextStroke()) {
          nsTextFrame* f = static_cast<nsTextFrame*>(frame);
          r = f->RecomputeOverflow(LineContainerFrame());
        }
        frame->FinishAndStoreOverflow(r, frame->GetSize());
      }
    }

    overflowAreas.UnionWith(r + frame->GetPosition());
  }

  // Also compute relative position in the annotations.
  if (psd->mFrame->mFrame->IsRubyFrame()) {
    RelativePositionAnnotations(psd, overflowAreas);
  }

  // If we just computed a spans combined area, we need to update its
  // overflow rect...
  if (psd != mRootSpan) {
    PerFrameData* spanPFD = psd->mFrame;
    nsIFrame* frame = spanPFD->mFrame;
    frame->FinishAndStoreOverflow(overflowAreas, frame->GetSize());
  }
  aOverflowAreas = overflowAreas;
}
