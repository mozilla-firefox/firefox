/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* representation of one line within a block frame, a CSS line box */

#ifndef nsLineBox_h_
#define nsLineBox_h_

#include <algorithm>

#include "mozilla/Likely.h"
#include "nsIFrame.h"
#include "nsILineIterator.h"
#include "nsTHashSet.h"

class nsLineBox;
class nsWindowSizes;

namespace mozilla {
class PresShell;
}  // namespace mozilla

/**
 * Function to create a line box and initialize it with a single frame.
 * The allocation is infallible.
 * If the frame was moved from another line then you're responsible
 * for notifying that line using NoteFrameRemoved().  Alternatively,
 * it's better to use the next function that does that for you in an
 * optimal way.
 */
nsLineBox* NS_NewLineBox(mozilla::PresShell* aPresShell, nsIFrame* aFrame,
                         bool aIsBlock);
/**
 * Function to create a line box and initialize it with aCount frames
 * that are currently on aFromLine.  The allocation is infallible.
 */
nsLineBox* NS_NewLineBox(mozilla::PresShell* aPresShell, nsLineBox* aFromLine,
                         nsIFrame* aFrame, int32_t aCount);

/**
 * Users must have the class that is to be part of the list inherit
 * from nsLineLink.  If they want to be efficient, it should be the
 * first base class.  (This was originally nsCLink in a templatized
 * nsCList, but it's still useful separately.)
 */

class nsLineList;

class nsLineLink {
  template <typename Link, bool>
  friend class GenericLineListIterator;
  friend class nsLineList;

  nsLineLink* _mNext;  // or head
  nsLineLink* _mPrev;  // or tail
};

/**
 * The nsLineBox class represents a horizontal line of frames. It contains
 * enough state to support incremental reflow of the frames, event handling
 * for the frames, and rendering of the frames.
 */
class nsLineBox final : public nsLineLink {
 private:
  nsLineBox(nsIFrame* aFrame, int32_t aCount, bool aIsBlock);
  ~nsLineBox();

  // Infallible overloaded new operator. Uses an arena (which comes from the
  // presShell) to perform the allocation.
  void* operator new(size_t sz, mozilla::PresShell* aPresShell);

 public:
  void operator delete(void* aPtr, size_t sz) = delete;
  // Use these functions to allocate and destroy line boxes
  friend nsLineBox* NS_NewLineBox(mozilla::PresShell* aPresShell,
                                  nsIFrame* aFrame, bool aIsBlock);
  friend nsLineBox* NS_NewLineBox(mozilla::PresShell* aPresShell,
                                  nsLineBox* aFromLine, nsIFrame* aFrame,
                                  int32_t aCount);
  void Destroy(mozilla::PresShell* aPresShell);

  // mBlock bit
  bool IsBlock() const { return mFlags.mBlock; }
  bool IsInline() const { return !mFlags.mBlock; }

  // mDirty bit
  void MarkDirty() { mFlags.mDirty = 1; }
  void ClearDirty() { mFlags.mDirty = 0; }
  bool IsDirty() const { return mFlags.mDirty; }

  // mPreviousMarginDirty bit
  void MarkPreviousMarginDirty() { mFlags.mPreviousMarginDirty = 1; }
  void ClearPreviousMarginDirty() { mFlags.mPreviousMarginDirty = 0; }
  bool IsPreviousMarginDirty() const { return mFlags.mPreviousMarginDirty; }

  // mHasClearance bit
  void SetHasClearance() { mFlags.mHasClearance = 1; }
  void ClearHasClearance() { mFlags.mHasClearance = 0; }
  bool HasClearance() const { return mFlags.mHasClearance; }

  // mImpactedByFloat bit
  void SetLineIsImpactedByFloat(bool aValue) {
    mFlags.mImpactedByFloat = aValue;
  }
  bool IsImpactedByFloat() const { return mFlags.mImpactedByFloat; }

  // mLineWrapped bit
  void SetLineWrapped(bool aOn) { mFlags.mLineWrapped = aOn; }
  bool IsLineWrapped() const { return mFlags.mLineWrapped; }

  // mInvalidateTextRuns bit
  void SetInvalidateTextRuns(bool aOn) { mFlags.mInvalidateTextRuns = aOn; }
  bool GetInvalidateTextRuns() const { return mFlags.mInvalidateTextRuns; }

  // mResizeReflowOptimizationDisabled bit
  void DisableResizeReflowOptimization() {
    mFlags.mResizeReflowOptimizationDisabled = true;
  }
  void EnableResizeReflowOptimization() {
    mFlags.mResizeReflowOptimizationDisabled = false;
  }
  bool ResizeReflowOptimizationDisabled() const {
    return mFlags.mResizeReflowOptimizationDisabled;
  }

  // mHasMarker bit
  void SetHasMarker() {
    mFlags.mHasMarker = true;
    InvalidateCachedIsEmpty();
  }
  void ClearHasMarker() {
    mFlags.mHasMarker = false;
    InvalidateCachedIsEmpty();
  }
  bool HasMarker() const { return mFlags.mHasMarker; }

  // mHadFloatPushed bit
  void SetHadFloatPushed() { mFlags.mHadFloatPushed = true; }
  void ClearHadFloatPushed() { mFlags.mHadFloatPushed = false; }
  bool HadFloatPushed() const { return mFlags.mHadFloatPushed; }

  // mHasLineClampEllipsis bit
  void SetHasLineClampEllipsis() { mFlags.mHasLineClampEllipsis = true; }
  void ClearHasLineClampEllipsis() { mFlags.mHasLineClampEllipsis = false; }
  bool HasLineClampEllipsis() const { return mFlags.mHasLineClampEllipsis; }

  // mMovedFragments bit
  void SetMovedFragments() { mFlags.mMovedFragments = true; }
  void ClearMovedFragments() { mFlags.mMovedFragments = false; }
  bool MovedFragments() const { return mFlags.mMovedFragments; }

 private:
  // Add a hash table for fast lookup when the line has more frames than this.
  static const uint32_t kMinChildCountForHashtable = 200;

  /**
   * Take ownership of aFromLine's hash table and remove the frames that
   * stay on aFromLine from it, i.e. aFromLineNewCount frames starting with
   * mFirstChild.  This method is used to optimize moving a large number
   * of frames from one line to the next.
   */
  void StealHashTableFrom(nsLineBox* aFromLine, uint32_t aFromLineNewCount);

  /**
   * Does the equivalent of this->NoteFrameAdded and aFromLine->NoteFrameRemoved
   * for each frame on this line, but in a optimized way.
   */
  void NoteFramesMovedFrom(nsLineBox* aFromLine);

  void SwitchToHashtable() {
    MOZ_ASSERT(!mFlags.mHasHashedFrames);
    uint32_t count = GetChildCount();
    mFlags.mHasHashedFrames = 1;
    uint32_t minLength =
        std::max(kMinChildCountForHashtable,
                 uint32_t(PLDHashTable::kDefaultInitialLength));
    mFrames = new nsTHashSet<nsIFrame*>(std::max(count, minLength));
    for (nsIFrame* f = mFirstChild; count-- > 0; f = f->GetNextSibling()) {
      mFrames->Insert(f);
    }
  }
  void SwitchToCounter() {
    MOZ_ASSERT(mFlags.mHasHashedFrames);
    uint32_t count = GetChildCount();
    delete mFrames;
    mFlags.mHasHashedFrames = 0;
    mChildCount = count;
  }

 public:
  int32_t GetChildCount() const {
    return MOZ_UNLIKELY(mFlags.mHasHashedFrames) ? mFrames->Count()
                                                 : mChildCount;
  }

  // An iterator over the child frames of a single line.
  class ChildFrameIterator {
   public:
    using value_type = nsIFrame*;
    using pointer = value_type*;
    using reference = value_type&;
    using difference_type = ptrdiff_t;
    // Stashing iterator (operator* returns by value), so the category can be
    // at most std::input_iterator_tag.
    using iterator_category = std::input_iterator_tag;

    ChildFrameIterator(nsIFrame* aFrame, int32_t aRemaining)
        : mCurrentFrame(aFrame), mRemainingChildCount(aRemaining) {}

    nsIFrame* operator*() const { return mCurrentFrame; }

    ChildFrameIterator& operator++() {
      MOZ_ASSERT(mRemainingChildCount > 0);
      --mRemainingChildCount;
      mCurrentFrame =
          mRemainingChildCount > 0 ? mCurrentFrame->GetNextSibling() : nullptr;
      return *this;
    }

    bool operator==(const ChildFrameIterator&) const = default;

   private:
    nsIFrame* mCurrentFrame;
    int32_t mRemainingChildCount;
  };

  class ChildFrameRange {
   public:
    explicit ChildFrameRange(const nsLineBox* aLine) : mLine(aLine) {}
    ChildFrameIterator begin() const {
      const int32_t count = mLine->GetChildCount();
      return {count > 0 ? mLine->mFirstChild : nullptr, count};
    }
    ChildFrameIterator end() const { return {nullptr, 0}; }

   private:
    const nsLineBox* mLine;
  };

  // Return a range over this line's child frames, suitable for use with a
  // range-based for loop.
  //
  // Example usage:
  //   for (nsIFrame* f : line->ChildFrames()) { ... }
  //
  // Note: use the range-based for loop only when the frame sibling chain in
  // this line is not mutated during traversal. If mutation is required, use
  // mFirstChild and GetChildCount() instead.
  ChildFrameRange ChildFrames() const { return ChildFrameRange(this); }

  /**
   * Register that aFrame is now on this line.
   */
  void NoteFrameAdded(nsIFrame* aFrame) {
    if (MOZ_UNLIKELY(mFlags.mHasHashedFrames)) {
      mFrames->Insert(aFrame);
    } else {
      if (++mChildCount >= kMinChildCountForHashtable) {
        SwitchToHashtable();
      }
    }
  }

  /**
   * Register that aFrame is not on this line anymore.
   */
  void NoteFrameRemoved(nsIFrame* aFrame) {
    MOZ_ASSERT(GetChildCount() > 0);
    if (MOZ_UNLIKELY(mFlags.mHasHashedFrames)) {
      mFrames->Remove(aFrame);
      if (mFrames->Count() < kMinChildCountForHashtable) {
        SwitchToCounter();
      }
    } else {
      --mChildCount;
    }
  }

  // mHasForcedLineBreakAfter bit & mFloatClearType value
  void ClearForcedLineBreak() {
    mFlags.mHasForcedLineBreakAfter = false;
    mFlags.mFloatClearType = mozilla::UsedClear::None;
  }

  bool HasFloatClearTypeBefore() const {
    return FloatClearTypeBefore() != mozilla::UsedClear::None;
  }
  void SetFloatClearTypeBefore(mozilla::UsedClear aClearType) {
    MOZ_ASSERT(IsBlock(), "Only block lines have break-before status!");
    MOZ_ASSERT(aClearType != mozilla::UsedClear::None,
               "Only UsedClear:Left/Right/Both are allowed before a line");
    mFlags.mFloatClearType = aClearType;
  }
  mozilla::UsedClear FloatClearTypeBefore() const {
    return IsBlock() ? mFlags.mFloatClearType : mozilla::UsedClear::None;
  }

  bool HasForcedLineBreakAfter() const {
    MOZ_ASSERT(IsInline() || !mFlags.mHasForcedLineBreakAfter,
               "A block line shouldn't set mHasForcedLineBreakAfter bit!");
    return IsInline() && mFlags.mHasForcedLineBreakAfter;
  }
  void SetForcedLineBreakAfter(mozilla::UsedClear aClearType) {
    MOZ_ASSERT(IsInline(), "Only inline lines have break-after status!");
    mFlags.mHasForcedLineBreakAfter = true;
    mFlags.mFloatClearType = aClearType;
  }
  bool HasFloatClearTypeAfter() const {
    return FloatClearTypeAfter() != mozilla::UsedClear::None;
  }
  mozilla::UsedClear FloatClearTypeAfter() const {
    return IsInline() ? mFlags.mFloatClearType : mozilla::UsedClear::None;
  }

  // mCarriedOutBEndMargin value
  mozilla::CollapsingMargin GetCarriedOutBEndMargin() const;
  // Returns true if the margin changed
  bool SetCarriedOutBEndMargin(mozilla::CollapsingMargin aValue);

  // mFloats
  bool HasFloats() const {
    return (IsInline() && mInlineData) && !mInlineData->mFloats.IsEmpty();
  }
  const nsTArray<nsIFrame*>& Floats() const {
    MOZ_ASSERT(HasFloats());
    return mInlineData->mFloats;
  }
  // Append aFloats to mFloat. aFloats will be empty.
  void AppendFloats(nsTArray<nsIFrame*>&& aFloats);
  void ClearFloats();
  bool RemoveFloat(nsIFrame* aFrame);

  // The ink overflow area should never be used for things that affect layout.
  // The scrollable overflow area are permitted to affect layout for handling of
  // overflow and scrollbars.
  void SetOverflowAreas(const mozilla::OverflowAreas& aOverflowAreas);
  mozilla::LogicalRect GetOverflowArea(mozilla::OverflowType aType,
                                       mozilla::WritingMode aWM,
                                       const nsSize& aContainerSize) {
    return mozilla::LogicalRect(aWM, GetOverflowArea(aType), aContainerSize);
  }
  nsRect GetOverflowArea(mozilla::OverflowType aType) const {
    return mData ? mData->mOverflowAreas.Overflow(aType) : GetPhysicalBounds();
  }
  mozilla::OverflowAreas GetOverflowAreas() const {
    if (mData) {
      return mData->mOverflowAreas;
    }
    nsRect bounds = GetPhysicalBounds();
    return mozilla::OverflowAreas(bounds, bounds);
  }
  nsRect InkOverflowRect() const {
    return GetOverflowArea(mozilla::OverflowType::Ink);
  }
  nsRect ScrollableOverflowRect() const {
    return GetOverflowArea(mozilla::OverflowType::Scrollable);
  }

  // See comment on `mInFlowChildBounds`.
  void SetInFlowChildBounds(const mozilla::Maybe<nsRect>& aInFlowChildBounds);
  mozilla::Maybe<nsRect> GetInFlowChildBounds() const;

  void SlideBy(nscoord aDBCoord, const nsSize& aContainerSize) {
    NS_ASSERTION(
        aContainerSize == mContainerSize || mContainerSize == nsSize(-1, -1),
        "container size doesn't match");
    mContainerSize = aContainerSize;
    mBounds.BStart(mWritingMode) += aDBCoord;
    if (mData) {
      // Use a null containerSize to convert vector from logical to physical.
      const nsSize nullContainerSize;
      nsPoint physicalDelta =
          mozilla::LogicalPoint(mWritingMode, 0, aDBCoord)
              .GetPhysicalPoint(mWritingMode, nullContainerSize);
      for (const auto otype : mozilla::AllOverflowTypes()) {
        mData->mOverflowAreas.Overflow(otype) += physicalDelta;
      }
      if (mData->mInFlowChildBounds) {
        *mData->mInFlowChildBounds += physicalDelta;
      }
    }
  }

  // Container-size for the line is changing (and therefore if writing mode
  // was vertical-rl, the line will move physically; this is like SlideBy,
  // but it is the container size instead of the line's own logical coord
  // that is changing.
  nsSize UpdateContainerSize(const nsSize aNewContainerSize) {
    NS_ASSERTION(mContainerSize != nsSize(-1, -1), "container size not set");
    nsSize delta = mContainerSize - aNewContainerSize;
    mContainerSize = aNewContainerSize;
    // this has a physical-coordinate effect only in vertical-rl mode
    if (mWritingMode.IsVerticalRL() && mData) {
      nsPoint physicalDelta(-delta.width, 0);
      for (const auto otype : mozilla::AllOverflowTypes()) {
        mData->mOverflowAreas.Overflow(otype) += physicalDelta;
      }
      if (mData->mInFlowChildBounds) {
        *mData->mInFlowChildBounds += physicalDelta;
      }
    }
    return delta;
  }

  void IndentBy(nscoord aDICoord, const nsSize& aContainerSize) {
    NS_ASSERTION(
        aContainerSize == mContainerSize || mContainerSize == nsSize(-1, -1),
        "container size doesn't match");
    mContainerSize = aContainerSize;
    mBounds.IStart(mWritingMode) += aDICoord;
  }

  void ExpandBy(nscoord aDISize, const nsSize& aContainerSize) {
    NS_ASSERTION(
        aContainerSize == mContainerSize || mContainerSize == nsSize(-1, -1),
        "container size doesn't match");
    mContainerSize = aContainerSize;
    mBounds.ISize(mWritingMode) += aDISize;
  }

  /**
   * The logical ascent (distance from block-start to baseline) of the
   * linebox is the logical ascent of the anonymous inline box (for
   * which we don't actually create a frame) that wraps all the
   * consecutive inline children of a block.
   *
   * This is currently unused for block lines.
   */
  nscoord GetLogicalAscent() const { return mAscent; }
  void SetLogicalAscent(nscoord aAscent) { mAscent = aAscent; }

  nscoord BStart() const { return mBounds.BStart(mWritingMode); }
  nscoord BSize() const { return mBounds.BSize(mWritingMode); }
  nscoord BEnd() const { return mBounds.BEnd(mWritingMode); }
  nscoord IStart() const { return mBounds.IStart(mWritingMode); }
  nscoord ISize() const { return mBounds.ISize(mWritingMode); }
  nscoord IEnd() const { return mBounds.IEnd(mWritingMode); }
  void SetBoundsEmpty() {
    mBounds.IStart(mWritingMode) = 0;
    mBounds.ISize(mWritingMode) = 0;
    mBounds.BStart(mWritingMode) = 0;
    mBounds.BSize(mWritingMode) = 0;
  }

  using DestroyContext = nsIFrame::DestroyContext;
  static void DeleteLineList(nsPresContext* aPresContext, nsLineList& aLines,
                             nsFrameList* aFrames, DestroyContext&);

  // search from end to beginning of [aBegin, aEnd)
  // Returns true if it found the line and false if not.
  // Moves aEnd as it searches so that aEnd points to the resulting line.
  // aLastFrameBeforeEnd is the last frame before aEnd (so if aEnd is
  // the end of the line list, it's just the last frame in the frame
  // list).
  static bool RFindLineContaining(nsIFrame* aFrame,
                                  const LineListIterator& aBegin,
                                  LineListIterator& aEnd,
                                  nsIFrame* aLastFrameBeforeEnd,
                                  int32_t* aFrameIndexInLine);

#ifdef DEBUG_FRAME_DUMP
  static const char* UsedClearToString(mozilla::UsedClear aClearType);

  void List(FILE* out, int32_t aIndent,
            nsIFrame::ListFlags aFlags = nsIFrame::ListFlags()) const;
  void List(FILE* out = stderr, const char* aPrefix = "",
            nsIFrame::ListFlags aFlags = nsIFrame::ListFlags()) const;
  nsIFrame* LastChild() const;
#endif

  void AddSizeOfExcludingThis(nsWindowSizes& aSizes) const;

  // Find the index of aFrame within the line, starting search at the start.
  int32_t IndexOf(const nsIFrame* aFrame) const;

  // Find the index of aFrame within the line, starting search from both ends
  // of the line and working inwards.
  // (Produces the same result as IndexOf, but with different performance
  // characteristics.)  The caller must provide the last frame in the line.
  int32_t RLIndexOf(const nsIFrame* aFrame,
                    const nsIFrame* aLastFrameInLine) const;

  bool Contains(const nsIFrame* aFrame) const {
    return MOZ_UNLIKELY(mFlags.mHasHashedFrames) ? mFrames->Contains(aFrame)
                                                 : IndexOf(aFrame) >= 0;
  }

  // whether the line box is "logically" empty (just like nsIFrame::IsEmpty)
  bool IsEmpty() const;

  // Call this only while in Reflow() for the block the line belongs
  // to, only between reflowing the line (or sliding it, if we skip
  // reflowing it) and the end of reflowing the block.
  bool CachedIsEmpty();

  void InvalidateCachedIsEmpty() { mFlags.mEmptyCacheValid = false; }

  // For debugging purposes
  bool IsValidCachedIsEmpty() { return mFlags.mEmptyCacheValid; }

#ifdef DEBUG
  static int32_t GetCtorCount();
#endif

  nsIFrame* mFirstChild;

  mozilla::WritingMode mWritingMode;

  // Physical size. Use only for physical <-> logical coordinate conversion.
  nsSize mContainerSize;

 private:
  mozilla::LogicalRect mBounds;

 public:
  const mozilla::LogicalRect& GetBounds() { return mBounds; }
  nsRect GetPhysicalBounds() const {
    if (mBounds.IsAllZero()) {
      return nsRect(0, 0, 0, 0);
    }

    NS_ASSERTION(mContainerSize != nsSize(-1, -1),
                 "mContainerSize not initialized");
    return mBounds.GetPhysicalRect(mWritingMode, mContainerSize);
  }
  void SetBounds(mozilla::WritingMode aWritingMode, nscoord aIStart,
                 nscoord aBStart, nscoord aISize, nscoord aBSize,
                 const nsSize& aContainerSize) {
    mWritingMode = aWritingMode;
    mContainerSize = aContainerSize;
    mBounds =
        mozilla::LogicalRect(aWritingMode, aIStart, aBStart, aISize, aBSize);
  }

  // mFlags.mHasHashedFrames says which one to use
  union {
    nsTHashSet<nsIFrame*>* mFrames;
    uint32_t mChildCount;
  };

  struct FlagBits {
    bool mDirty : 1;
    bool mPreviousMarginDirty : 1;
    bool mHasClearance : 1;
    bool mBlock : 1;
    bool mImpactedByFloat : 1;
    bool mLineWrapped : 1;
    bool mInvalidateTextRuns : 1;
    // default 0 = means that the opt potentially applies to this line.
    // 1 = never skip reflowing this line for a resize reflow
    bool mResizeReflowOptimizationDisabled : 1;
    bool mEmptyCacheValid : 1;
    bool mEmptyCacheState : 1;
    // mHasMarker indicates that this is an inline line whose block's
    // ::marker is adjacent to this line and non-empty.
    bool mHasMarker : 1;
    // Indicates that this line *may* have a placeholder for a float
    // that was pushed to a later column or page.
    bool mHadFloatPushed : 1;
    bool mHasHashedFrames : 1;
    // Indicates that this line is the one identified by an ancestor block
    // with -webkit-line-clamp on its legacy flex container, and that subsequent
    // lines under that block are "clamped" away, and therefore we need to
    // render a 'text-overflow: ellipsis'-like marker in this line.  At most one
    // line in the set of lines found by LineClampLineIterator for a given
    // block will have this flag set.
    bool mHasLineClampEllipsis : 1;
    // Has this line moved to a different fragment of the block since
    // the last time it was reflowed?
    bool mMovedFragments : 1;
    // mHasForcedLineBreakAfter indicates that this *inline* line has a
    // break-after status due to a float clearance or ending with <br>. A block
    // line shouldn't set this bit.
    //
    // Note: This bit is unrelated to CSS break-after property because it is all
    // about line break-after for inline-level boxes.
    bool mHasForcedLineBreakAfter : 1;
    // mFloatClearType indicates that there's a float clearance before a block
    // line, or after an inline line.
    mozilla::UsedClear mFloatClearType;
  };

  struct ExtraData {
    explicit ExtraData(const nsRect& aBounds)
        : mOverflowAreas(aBounds, aBounds) {}
    mozilla::OverflowAreas mOverflowAreas;
    // Union of the margin-boxes of our in-flow children (only children,
    // *not* their descendants). This is part of a special contribution to
    // the scrollable overflow of a scrolled block; as such, this is only
    // emplaced if our block is a scrolled frame (and we have in-flow children,
    // and floats, which are considered in-flow for scrollable overflow).
    mozilla::Maybe<nsRect> mInFlowChildBounds;
  };

  struct ExtraBlockData : public ExtraData {
    explicit ExtraBlockData(const nsRect& aBounds) : ExtraData(aBounds) {}
    mozilla::CollapsingMargin mCarriedOutBEndMargin;
  };

  struct ExtraInlineData : public ExtraData {
    explicit ExtraInlineData(const nsRect& aBounds)
        : ExtraData(aBounds),
          mFloatEdgeIStart(nscoord_MIN),
          mFloatEdgeIEnd(nscoord_MIN) {}
    nscoord mFloatEdgeIStart;
    nscoord mFloatEdgeIEnd;
    nsTArray<nsIFrame*> mFloats;
  };

  bool GetFloatEdges(nscoord* aStart, nscoord* aEnd) const {
    MOZ_ASSERT(IsInline(), "block line can't have float edges");
    if (mInlineData && mInlineData->mFloatEdgeIStart != nscoord_MIN) {
      *aStart = mInlineData->mFloatEdgeIStart;
      *aEnd = mInlineData->mFloatEdgeIEnd;
      return true;
    }
    return false;
  }
  void SetFloatEdges(nscoord aStart, nscoord aEnd);
  void ClearFloatEdges();

 protected:
  nscoord mAscent;  // see |SetAscent| / |GetAscent|
  static_assert(sizeof(FlagBits) <= sizeof(uint32_t),
                "size of FlagBits should not be larger than size of uint32_t");
  union {
    uint32_t mAllFlags;
    FlagBits mFlags;
  };

  union {
    ExtraData* mData;
    ExtraBlockData* mBlockData;
    ExtraInlineData* mInlineData;
  };

  void Cleanup();
  void MaybeFreeData();
};

/**
 * A linked list type where the items in the list must inherit from
 * a link type to fuse allocations.
 *
 * API heavily based on the |list| class in the C++ standard.
 */

template <typename Link, bool IsReverse>
class GenericLineListIterator {
  template <typename OtherLink, bool>
  friend class GenericLineListIterator;

 public:
  friend class nsLineList;

  using self_type = GenericLineListIterator<Link, IsReverse>;
  static constexpr bool is_const = std::is_const_v<Link>;

  using const_reference = const nsLineBox&;
  using const_pointer = const nsLineBox*;
  using reference = std::conditional_t<is_const, const_reference, nsLineBox&>;
  using pointer = std::conditional_t<is_const, const_pointer, nsLineBox*>;
  using size_type = uint32_t;
  using link_type = Link;

#ifdef DEBUG
  GenericLineListIterator() : mListLink(nullptr) {
    memset(&mCurrent, 0xcd, sizeof(mCurrent));
  }
#else
  // Auto generated default constructor OK.
#endif
  // Auto generated copy-constructor OK.

  template <typename OtherLink, bool OtherIsReverse>
  self_type& operator=(
      const GenericLineListIterator<OtherLink, OtherIsReverse>& aOther) {
    mCurrent = aOther.mCurrent;
#ifdef DEBUG
    mListLink = aOther.mListLink;
#endif
    return *this;
  }

  self_type& SetPosition(pointer p) {
    mCurrent = p;
    return *this;
  }

  self_type& operator++() {
    mCurrent = IsReverse ? mCurrent->_mPrev : mCurrent->_mNext;
    return *this;
  }

  self_type operator++(int) {
    self_type rv(*this);
    mCurrent = IsReverse ? mCurrent->_mPrev : mCurrent->_mNext;
    return rv;
  }

  self_type& operator--() {
    mCurrent = IsReverse ? mCurrent->_mNext : mCurrent->_mPrev;
    return *this;
  }

  self_type operator--(int) {
    self_type rv(*this);
    mCurrent = IsReverse ? mCurrent->_mNext : mCurrent->_mPrev;
    return rv;
  }

  pointer get() {
    MOZ_ASSERT(mListLink);
    MOZ_ASSERT(mCurrent != mListLink, "running past end");
    return static_cast<pointer>(mCurrent);
  }

  const_pointer get() const {
    MOZ_ASSERT(mListLink);
    MOZ_ASSERT(mCurrent != mListLink, "running past end");
    return static_cast<const_pointer>(mCurrent);
  }

  reference operator*() { return *get(); }
  pointer operator->() { return get(); }
  operator pointer() { return get(); }
  const_reference operator*() const { return *get(); }
  const_pointer operator->() const { return get(); }
  operator const_pointer() const { return get(); }

  self_type next() {
    self_type copy(*this);
    return ++copy;
  }

  self_type next() const {
    self_type copy(*this);
    return ++copy;
  }

  self_type prev() {
    self_type copy(*this);
    return --copy;
  }

  self_type prev() const {
    self_type copy(*this);
    return --copy;
  }

  bool operator==(const self_type& aOther) const {
    MOZ_ASSERT(mListLink);
    MOZ_ASSERT(mListLink == aOther.mListLink,
               "comparing iterators over different lists");
    return mCurrent == aOther.mCurrent;
  }
  bool operator!=(const self_type&) const = default;

#ifdef DEBUG
  bool IsInSameList(const self_type& aOther) const {
    return mListLink == aOther.mListLink;
  }
#endif

 private:
  link_type* mCurrent;
#ifdef DEBUG
  link_type* mListLink;  // the list's link, i.e., the end
#endif
};

class nsLineList {
 public:
  using self_type = nsLineList;
  using const_reference = const nsLineBox&;
  using pointer = nsLineBox*;
  using const_pointer = const nsLineBox*;
  using size_type = uint32_t;
  using link_type = nsLineLink;

 private:
  link_type mLink;

 public:
  using iterator = GenericLineListIterator<nsLineLink, false>;
  using reverse_iterator = GenericLineListIterator<nsLineLink, true>;
  using const_iterator = GenericLineListIterator<const nsLineLink, false>;
  using const_reverse_iterator =
      GenericLineListIterator<const nsLineLink, true>;

  nsLineList() {
    MOZ_COUNT_CTOR(nsLineList);
    clear();
  }

  MOZ_COUNTED_DTOR(nsLineList)

  const_iterator begin() const {
    const_iterator rv;
    rv.mCurrent = mLink._mNext;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  iterator begin() {
    iterator rv;
    rv.mCurrent = mLink._mNext;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  iterator begin(nsLineBox* aLine) {
    iterator rv;
    rv.mCurrent = aLine;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  const_iterator end() const {
    const_iterator rv;
    rv.mCurrent = &mLink;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  iterator end() {
    iterator rv;
    rv.mCurrent = &mLink;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  const_reverse_iterator rbegin() const {
    const_reverse_iterator rv;
    rv.mCurrent = mLink._mPrev;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  reverse_iterator rbegin() {
    reverse_iterator rv;
    rv.mCurrent = mLink._mPrev;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  reverse_iterator rbegin(nsLineBox* aLine) {
    reverse_iterator rv;
    rv.mCurrent = aLine;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  const_reverse_iterator rend() const {
    const_reverse_iterator rv;
    rv.mCurrent = &mLink;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  reverse_iterator rend() {
    reverse_iterator rv;
    rv.mCurrent = &mLink;
#ifdef DEBUG
    rv.mListLink = &mLink;
#endif
    return rv;
  }

  bool empty() const { return mLink._mNext == &mLink; }

  // NOTE: O(N).
  size_type size() const {
    size_type count = 0;
    for (const link_type* cur = mLink._mNext; cur != &mLink;
         cur = cur->_mNext) {
      ++count;
    }
    return count;
  }

  pointer front() {
    NS_ASSERTION(!empty(), "no element to return");
    return static_cast<pointer>(mLink._mNext);
  }

  const_pointer front() const {
    NS_ASSERTION(!empty(), "no element to return");
    return static_cast<const_pointer>(mLink._mNext);
  }

  pointer back() {
    NS_ASSERTION(!empty(), "no element to return");
    return static_cast<pointer>(mLink._mPrev);
  }

  const_pointer back() const {
    NS_ASSERTION(!empty(), "no element to return");
    return static_cast<const_pointer>(mLink._mPrev);
  }

  void push_front(pointer aNew) {
    aNew->_mNext = mLink._mNext;
    mLink._mNext->_mPrev = aNew;
    aNew->_mPrev = &mLink;
    mLink._mNext = aNew;
  }

  void pop_front()
  // NOTE: leaves dangling next/prev pointers
  {
    NS_ASSERTION(!empty(), "no element to pop");
    link_type* newFirst = mLink._mNext->_mNext;
    newFirst->_mPrev = &mLink;
    // mLink._mNext->_mNext = nullptr;
    // mLink._mNext->_mPrev = nullptr;
    mLink._mNext = newFirst;
  }

  void push_back(pointer aNew) {
    aNew->_mPrev = mLink._mPrev;
    mLink._mPrev->_mNext = aNew;
    aNew->_mNext = &mLink;
    mLink._mPrev = aNew;
  }

  void pop_back()
  // NOTE: leaves dangling next/prev pointers
  {
    NS_ASSERTION(!empty(), "no element to pop");
    link_type* newLast = mLink._mPrev->_mPrev;
    newLast->_mNext = &mLink;
    // mLink._mPrev->_mPrev = nullptr;
    // mLink._mPrev->_mNext = nullptr;
    mLink._mPrev = newLast;
  }

  // inserts x before position
  iterator before_insert(iterator position, pointer x) {
    // use |mCurrent| to prevent DEBUG_PASS_END assertions
    x->_mPrev = position.mCurrent->_mPrev;
    x->_mNext = position.mCurrent;
    position.mCurrent->_mPrev->_mNext = x;
    position.mCurrent->_mPrev = x;
    return --position;
  }

  // inserts x after position
  iterator after_insert(iterator position, pointer x) {
    // use |mCurrent| to prevent DEBUG_PASS_END assertions
    x->_mNext = position.mCurrent->_mNext;
    x->_mPrev = position.mCurrent;
    position.mCurrent->_mNext->_mPrev = x;
    position.mCurrent->_mNext = x;
    return ++position;
  }

  // returns iterator pointing to after the element
  iterator erase(iterator position)
  // NOTE: leaves dangling next/prev pointers (except in DEBUG build)
  {
    position->_mPrev->_mNext = position->_mNext;
    position->_mNext->_mPrev = position->_mPrev;
#ifdef DEBUG
    nsLineLink* dead = position;
    iterator next = ++position;
    memset(dead, 0, sizeof(*dead));
    return next;
#else
    return ++position;
#endif
  }

  void swap(self_type& y) {
    link_type tmp(y.mLink);
    y.mLink = mLink;
    mLink = tmp;

    if (!empty()) {
      mLink._mNext->_mPrev = &mLink;
      mLink._mPrev->_mNext = &mLink;
    }

    if (!y.empty()) {
      y.mLink._mNext->_mPrev = &y.mLink;
      y.mLink._mPrev->_mNext = &y.mLink;
    }
  }

  void clear()
  // NOTE:  leaves dangling next/prev pointers
  {
    mLink._mNext = &mLink;
    mLink._mPrev = &mLink;
  }

  // inserts the conts of x before position and makes x empty
  void splice(iterator position, self_type& x) {
    // use |mCurrent| to prevent DEBUG_PASS_END assertions
    position.mCurrent->_mPrev->_mNext = x.mLink._mNext;
    x.mLink._mNext->_mPrev = position.mCurrent->_mPrev;
    x.mLink._mPrev->_mNext = position.mCurrent;
    position.mCurrent->_mPrev = x.mLink._mPrev;
    x.clear();
  }

  // Inserts element *i from list x before position and removes
  // it from x.
  void splice(iterator position, self_type& x, iterator i) {
    NS_ASSERTION(!x.empty(), "Can't insert from empty list.");
    NS_ASSERTION(position != i && position.mCurrent != i->_mNext,
                 "We don't check for this case.");

    // remove from |x|
    i->_mPrev->_mNext = i->_mNext;
    i->_mNext->_mPrev = i->_mPrev;

    // use |mCurrent| to prevent DEBUG_PASS_END assertions
    // link into |this|, before-side
    i->_mPrev = position.mCurrent->_mPrev;
    position.mCurrent->_mPrev->_mNext = i.get();

    // link into |this|, after-side
    i->_mNext = position.mCurrent;
    position.mCurrent->_mPrev = i.get();
  }

  // Inserts elements in [|first|, |last|), which are in |x|,
  // into |this| before |position| and removes them from |x|.
  void splice(iterator position, self_type& x, iterator first, iterator last) {
    NS_ASSERTION(!x.empty(), "Can't insert from empty list.");

    if (first == last) {
      return;
    }

    --last;  // so we now want to move [first, last]
    // remove from |x|
    first->_mPrev->_mNext = last->_mNext;
    last->_mNext->_mPrev = first->_mPrev;

    // use |mCurrent| to prevent DEBUG_PASS_END assertions
    // link into |this|, before-side
    first->_mPrev = position.mCurrent->_mPrev;
    position.mCurrent->_mPrev->_mNext = first.get();

    // link into |this|, after-side
    last->_mNext = position.mCurrent;
    position.mCurrent->_mPrev = last.get();
  }
};

//----------------------------------------------------------------------

class nsLineIterator final : public nsILineIterator {
 public:
  nsLineIterator(const nsLineList& aLines, bool aRightToLeft)
      : mLines(aLines), mRightToLeft(aRightToLeft) {
    mIter = mLines.begin();
    if (mIter != mLines.end()) {
      mIndex = 0;
    }
  }

  int32_t GetNumLines() const final {
    if (mNumLines < 0) {
      mNumLines = int32_t(mLines.size());  // This is O(N) in number of lines!
    }
    return mNumLines;
  }

  bool IsLineIteratorFlowRTL() final { return mRightToLeft; }

  // Note that this updates the iterator's current position!
  mozilla::Result<LineInfo, nsresult> GetLine(int32_t aLineNumber) final;

  int32_t FindLineContaining(const nsIFrame* aFrame,
                             int32_t aStartLine = 0) final;

  NS_IMETHOD FindFrameAt(int32_t aLineNumber, nsPoint aPos,
                         nsIFrame** aFrameFound, bool* aPosIsBeforeFirstFrame,
                         bool* aPosIsAfterLastFrame) final;

  NS_IMETHOD CheckLineOrder(int32_t aLine, bool* aIsReordered,
                            nsIFrame** aFirstVisual,
                            nsIFrame** aLastVisual) final;

  nsLineIterator() = delete;
  nsLineIterator(const nsLineIterator& aOther) = delete;

 private:
  const nsLineBox* GetNextLine() {
    MOZ_ASSERT(mIter != mLines.end(), "Already at end!");
    ++mIndex;
    ++mIter;
    if (mIter == mLines.end()) {
      MOZ_ASSERT(mNumLines < 0 || mNumLines == mIndex);
      mNumLines = mIndex;
      return nullptr;
    }
    return mIter.get();
  }

  // Note that this updates the iterator's current position to the given line.
  const nsLineBox* GetLineAt(int32_t aIndex) {
    MOZ_ASSERT(mIndex >= 0);
    if (aIndex < 0 || (mNumLines >= 0 && aIndex >= mNumLines)) {
      return nullptr;
    }
    // Check if we should start counting lines from mIndex, or reset to the
    // start or end of the list and count from there (if the requested index is
    // closer to an end than to the current position).
    if (aIndex < mIndex / 2) {
      // Reset to the beginning and search from there.
      mIter = mLines.begin();
      mIndex = 0;
    } else if (mNumLines > 0 && aIndex > (mNumLines + mIndex) / 2) {
      // Jump to the end and search back from there.
      mIter = mLines.end();
      --mIter;
      mIndex = mNumLines - 1;
    }
    while (mIndex > aIndex) {
      // This cannot run past the start of the list, because we checked that
      // aIndex is non-negative.
      --mIter;
      --mIndex;
    }
    while (mIndex < aIndex) {
      // Here we have to check for reaching the end, as aIndex could be out of
      // range (if mNumLines was not initialized, so we couldn't range-check
      // aIndex on entry).
      if (mIter == mLines.end()) {
        MOZ_ASSERT(mNumLines < 0 || mNumLines == mIndex);
        mNumLines = mIndex;
        return nullptr;
      }
      ++mIter;
      ++mIndex;
    }
    return mIter.get();
  }

  const nsLineList& mLines;
  nsLineList::const_iterator mIter;
  int32_t mIndex = -1;
  mutable int32_t mNumLines = -1;
  const bool mRightToLeft;
};

#endif /* nsLineBox_h_ */
