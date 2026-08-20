/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_RubyUtils_h_
#define mozilla_RubyUtils_h_

#include "nsIFrame.h"
#include "nsTArray.h"

#define RTC_ARRAY_SIZE 1

class nsRubyFrame;
class nsRubyBaseFrame;
class nsRubyTextFrame;
class nsRubyContentFrame;
class nsRubyBaseContainerFrame;
class nsRubyTextContainerFrame;

namespace mozilla {

/**
 * Reserved ISize
 *
 * With some exceptions, each ruby internal box has two isizes, which
 * are the reflowed isize and the final isize. The reflowed isize is
 * what a box itself needs. It is determined when the box gets reflowed.
 *
 * The final isize is what a box should be as the final result. For a
 * ruby base/text box, the final isize is the size of its ruby column.
 * For a ruby base/text container, the final isize is the size of its
 * ruby segment. The final isize is never smaller than the reflowed
 * isize. It is initially determined when a ruby column/segment gets
 * fully reflowed, and may be advanced when a box is expanded, e.g.
 * for justification.
 *
 * The difference between the reflowed isize and the final isize is
 * reserved in the line layout after reflowing a box, hence it is called
 * "Reserved ISize" here. It is used to expand the ruby boxes from their
 * reflowed isize to the final isize during alignment of the line.
 *
 * There are three exceptions for the final isize:
 * 1. A ruby text container has a larger final isize only if it is for
 *    a span or collapsed annotations.
 * 2. A ruby base container has a larger final isize only if at least
 *    one of its ruby text containers does.
 * 3. If a ruby text container has a larger final isize, its children
 *    must not have.
 */

class RubyUtils {
 public:
  static inline bool IsRubyContentBox(LayoutFrameType aFrameType) {
    return aFrameType == mozilla::LayoutFrameType::RubyBase ||
           aFrameType == mozilla::LayoutFrameType::RubyText;
  }

  static inline bool IsRubyContainerBox(LayoutFrameType aFrameType) {
    return aFrameType == mozilla::LayoutFrameType::RubyBaseContainer ||
           aFrameType == mozilla::LayoutFrameType::RubyTextContainer;
  }

  static inline bool IsRubyBox(LayoutFrameType aFrameType) {
    return aFrameType == mozilla::LayoutFrameType::Ruby ||
           IsRubyContentBox(aFrameType) || IsRubyContainerBox(aFrameType);
  }

  static inline bool IsExpandableRubyBox(nsIFrame* aFrame) {
    mozilla::LayoutFrameType type = aFrame->Type();
    return IsRubyContentBox(type) || IsRubyContainerBox(type);
  }

  static inline bool IsRubyPseudo(PseudoStyleType aPseudo) {
    return aPseudo == PseudoStyleType::MozBlockRubyContent ||
           aPseudo == PseudoStyleType::MozRuby ||
           aPseudo == PseudoStyleType::MozRubyBase ||
           aPseudo == PseudoStyleType::MozRubyText ||
           aPseudo == PseudoStyleType::MozRubyBaseContainer ||
           aPseudo == PseudoStyleType::MozRubyTextContainer;
  }

  static void SetReservedISize(nsIFrame* aFrame, nscoord aISize);
  static void ClearReservedISize(nsIFrame* aFrame);
  static nscoord GetReservedISize(nsIFrame* aFrame);
};

/**
 * This array stores all ruby text containers of the ruby segment
 * of the given ruby base container.
 */
class MOZ_RAII AutoRubyTextContainerArray final
    : public AutoTArray<nsRubyTextContainerFrame*, RTC_ARRAY_SIZE> {
 public:
  explicit AutoRubyTextContainerArray(nsRubyBaseContainerFrame* aBaseContainer);
};

/**
 * This enumerator enumerates each ruby segment.
 */
class MOZ_STACK_CLASS RubySegmentEnumerator {
 public:
  explicit RubySegmentEnumerator(nsRubyFrame* aRubyFrame);

  void Next();
  bool AtEnd() const { return !mBaseContainer; }

  nsRubyBaseContainerFrame* GetBaseContainer() const { return mBaseContainer; }

 private:
  nsRubyBaseContainerFrame* mBaseContainer;
};

/**
 * Ruby column is a unit consists of one ruby base and all ruby
 * annotations paired with it.
 * See http://dev.w3.org/csswg/css-ruby/#ruby-pairing
 */
struct MOZ_STACK_CLASS RubyColumn {
  nsRubyBaseFrame* mBaseFrame;
  AutoTArray<nsRubyTextFrame*, RTC_ARRAY_SIZE> mTextFrames;
  bool mIsIntraLevelWhitespace;

  RubyColumn() : mBaseFrame(nullptr), mIsIntraLevelWhitespace(false) {}

  // Helper class to support iteration across the frames within a single
  // RubyColumn (the column's ruby base and its annotations).
  class MOZ_STACK_CLASS Iterator {
   public:
    nsIFrame* operator*() const;

    Iterator& operator++() {
      ++mIndex;
      SkipUntilExistingFrame();
      return *this;
    }
    Iterator operator++(int) {
      auto ret = *this;
      ++*this;
      return ret;
    }

    bool operator==(const Iterator& aIter2) const {
      MOZ_ASSERT(&mColumn == &aIter2.mColumn,
                 "Should only compare iterators of the same ruby column");
      return mIndex == aIter2.mIndex;
    }

   private:
    Iterator(const RubyColumn& aColumn, int32_t aIndex)
        : mColumn(aColumn), mIndex(aIndex) {
      MOZ_ASSERT(
          aIndex == -1 ||
          (aIndex >= 0 && aIndex <= int32_t(aColumn.mTextFrames.Length())));
      SkipUntilExistingFrame();
    }
    friend struct RubyColumn;  // for the constructor

    void SkipUntilExistingFrame();

    const RubyColumn& mColumn;
    // -1 means the ruby base frame,
    // non-negative means the index of ruby text frame
    // a value of mTextFrames.Length() means we're done iterating
    int32_t mIndex = -1;
  };

  Iterator begin() const { return Iterator(*this, -1); }
  Iterator end() const { return Iterator(*this, mTextFrames.Length()); }
  Iterator cbegin() const { return begin(); }
  Iterator cend() const { return end(); }
};

/**
 * This enumerator enumerates ruby columns in a segment.
 */
class MOZ_STACK_CLASS RubyColumnEnumerator {
 public:
  RubyColumnEnumerator(nsRubyBaseContainerFrame* aRBCFrame,
                       const AutoRubyTextContainerArray& aRTCFrames);

  void Next();
  bool AtEnd() const;

  uint32_t GetLevelCount() const { return mFrames.Length(); }
  nsRubyContentFrame* GetFrameAtLevel(uint32_t aIndex) const;
  void GetColumn(RubyColumn& aColumn) const;

 private:
  // Frames in this array are NOT necessary part of the current column.
  // When in doubt, use GetFrameAtLevel to access it.
  // See GetFrameAtLevel() and Next() for more info.
  AutoTArray<nsRubyContentFrame*, RTC_ARRAY_SIZE + 1> mFrames;
  // Whether we are on a column for intra-level whitespaces
  bool mAtIntraLevelWhitespace;
};

/**
 * Stores block-axis leadings produced from ruby annotations.
 */
struct RubyBlockLeadings {
  nscoord mStart = 0;
  nscoord mEnd = 0;

  void Reset() { mStart = mEnd = 0; }
  void Update(nscoord aStart, nscoord aEnd) {
    mStart = std::max(mStart, aStart);
    mEnd = std::max(mEnd, aEnd);
  }
  void Update(const RubyBlockLeadings& aOther) {
    Update(aOther.mStart, aOther.mEnd);
  }
};

}  // namespace mozilla

#endif /* !defined(mozilla_RubyUtils_h_) */
