/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsFrameList_h_
#define nsFrameList_h_

#include <stdio.h> /* for FILE* */

#include "mozilla/EnumSet.h"
#include "mozilla/FunctionTypeTraits.h"
#include "nsDebug.h"
#include "nsTArray.h"

#if defined(DEBUG) || defined(MOZ_DUMP_PAINTING) || defined(MOZ_LAYOUT_DEBUGGER)
// DEBUG_FRAME_DUMP enables nsIFrame::List and related methods.
// You can also define this in a non-DEBUG build if you need frame dumps.
#  define DEBUG_FRAME_DUMP 1
#endif

class nsContainerFrame;
class nsIContent;
class nsIFrame;
class nsPresContext;

namespace mozilla {

struct FrameDestroyContext;

class PresShell;
class FrameChildList;
enum class FrameChildListID {
  // The individual concrete child lists.
  Principal,
  Absolute,
  PushedAbsolute,
  Overflow,
  OverflowContainers,
  ExcessOverflowContainers,
  // Floats placed by the block. Their placeholders are in the block's in-flow
  // lines.
  Float,
  // Floats pushed to the block's next-in-flow because they didn't fit. The list
  // can hold floats' first-in-flows and next-in-flows, and it is expected to be
  // drained by the block's next-in-flow during reflow.
  PushedFloats,
  // Floats whose placeholders are in the overflow lines. This list collects the
  // floats' real frames so they are drained with their placeholders by the
  // block's next-in-flow.
  OverflowFloats,
  Marker,
  // A special alias for FrameChildListID::Principal that suppress the reflow
  // request that is normally done when manipulating child lists.
  NoReflowPrincipal,
};

}  // namespace mozilla

// Uncomment this to enable expensive frame-list integrity checking
// #define DEBUG_FRAME_LIST

/**
 * A class for managing a list of frames.
 */
class nsFrameList {
  // Next()/Prev() need to know about nsIFrame. To make them inline, their
  // implementations are in nsIFrame.h.
  struct ForwardFrameTraversal final {
    static inline nsIFrame* Next(nsIFrame*);
    static inline nsIFrame* Prev(nsIFrame*);
  };
  struct BackwardFrameTraversal final {
    static inline nsIFrame* Next(nsIFrame*);
    static inline nsIFrame* Prev(nsIFrame*);
  };

 public:
  template <typename FrameTraversal>
  class Iterator;
  class Slice;

  using iterator = Iterator<ForwardFrameTraversal>;
  using const_iterator = Iterator<ForwardFrameTraversal>;
  using reverse_iterator = Iterator<BackwardFrameTraversal>;
  using const_reverse_iterator = Iterator<BackwardFrameTraversal>;

  constexpr nsFrameList() : mFirstChild(nullptr), mLastChild(nullptr) {}

  nsFrameList(nsIFrame* aFirstFrame, nsIFrame* aLastFrame)
      : mFirstChild(aFirstFrame), mLastChild(aLastFrame) {
    VerifyList();
  }

  // nsFrameList is a move-only class by default. Use Clone() if you really want
  // a copy of this list.
  nsFrameList(const nsFrameList& aOther) = delete;
  nsFrameList& operator=(const nsFrameList& aOther) = delete;
  nsFrameList Clone() const { return nsFrameList(mFirstChild, mLastChild); }

  /**
   * Transfer frames in aOther to this list. aOther becomes empty after this
   * operation.
   */
  nsFrameList(nsFrameList&& aOther)
      : mFirstChild(aOther.mFirstChild), mLastChild(aOther.mLastChild) {
    aOther.Clear();
    VerifyList();
  }
  nsFrameList& operator=(nsFrameList&& aOther) {
    if (this != &aOther) {
      MOZ_ASSERT(IsEmpty(), "Assigning to a non-empty list will lose frames!");
      mFirstChild = aOther.FirstChild();
      mLastChild = aOther.LastChild();
      aOther.Clear();
    }
    return *this;
  }

  /**
   * Infallibly allocate a nsFrameList from the shell arena.
   */
  void* operator new(size_t sz, mozilla::PresShell* aPresShell);

  /**
   * Deallocate this list that was allocated from the shell arena.
   * The list is required to be empty.
   */
  void Delete(mozilla::PresShell* aPresShell);

  /**
   * For each frame in this list: remove it from the list then call
   * Destroy() on it with the passed context as an argument.
   */
  void DestroyFrames(mozilla::FrameDestroyContext&);

  void Clear() { mFirstChild = mLastChild = nullptr; }

  /**
   * Append aFrameList to this list.  If aParent is not null,
   * reparents the newly added frames.  Clears out aFrameList and
   * returns a list slice represening the newly-appended frames.
   */
  Slice AppendFrames(nsContainerFrame* aParent, nsFrameList&& aFrameList) {
    return InsertFrames(aParent, LastChild(), std::move(aFrameList));
  }

  /**
   * Append aFrame to this list.  If aParent is not null,
   * reparents the newly added frame.
   */
  void AppendFrame(nsContainerFrame* aParent, nsIFrame* aFrame) {
    AppendFrames(aParent, nsFrameList(aFrame, aFrame));
  }

  /**
   * Take aFrame out of the frame list. This also disconnects aFrame
   * from the sibling list. The frame must be non-null and present on
   * this list.
   */
  void RemoveFrame(nsIFrame* aFrame);

  /**
   * Take all the frames before aFrame out of the frame list; aFrame and all the
   * frames after it stay in this list. If aFrame is nullptr, remove the entire
   * frame list.
   * @param aFrame a frame in this frame list, or nullptr.
   * @return the removed frames, if any.
   */
  [[nodiscard]] nsFrameList TakeFramesBefore(nsIFrame* aFrame);

  /**
   * Take all the frames after aFrame out of the frame list; aFrame and all the
   * frames before it stay in this list. If aFrame is nullptr, removes the
   * entire list.
   * @param aFrame a frame in this list, or nullptr.
   * @return the removed frames, if any.
   */
  [[nodiscard]] nsFrameList TakeFramesAfter(nsIFrame* aFrame);

  /**
   * Take the first (or last) child (if any) out of the frame list.
   * @return the first (or last) child, or nullptr if the list is empty
   */
  nsIFrame* RemoveFirstChild();
  nsIFrame* RemoveLastChild();

  /**
   * The following two functions are intended to be used in concert for
   * removing a frame from its frame list when the set of possible frame
   * lists is known in advance, but the exact frame list is unknown.
   * aFrame must be non-null.
   * Example use:
   *   bool removed = frameList1.StartRemoveFrame(aFrame) ||
   *                  frameList2.ContinueRemoveFrame(aFrame) ||
   *                  frameList3.ContinueRemoveFrame(aFrame);
   *   MOZ_ASSERT(removed);
   *
   * @note One of the frame lists MUST contain aFrame, if it's on some other
   *       frame list then the example above will likely lead to crashes.
   * This function is O(1).
   * @return true iff aFrame was removed from /some/ list, not necessarily
   *         this one.  If it was removed from a different list then it is
   *         guaranteed that that list is still non-empty.
   * (this method is implemented in nsIFrame.h to be able to inline)
   */
  inline bool StartRemoveFrame(nsIFrame* aFrame);

  /**
   * Precondition: StartRemoveFrame MUST be called before this.
   * This function is O(1).
   * @see StartRemoveFrame
   * @return true iff aFrame was removed from this list
   * (this method is implemented in nsIFrame.h to be able to inline)
   */
  inline bool ContinueRemoveFrame(nsIFrame* aFrame);

  /**
   * Take a frame out of the frame list and then destroy it.
   * The frame must be non-null and present on this list.
   */
  void DestroyFrame(mozilla::FrameDestroyContext&, nsIFrame*);

  /**
   * Insert aFrame right after aPrevSibling, or prepend it to this
   * list if aPrevSibling is null. If aParent is not null, also
   * reparents newly-added frame. Note that this method always
   * sets the frame's nextSibling pointer.
   */
  void InsertFrame(nsContainerFrame* aParent, nsIFrame* aPrevSibling,
                   nsIFrame* aFrame) {
    InsertFrames(aParent, aPrevSibling, nsFrameList(aFrame, aFrame));
  }

  /**
   * Inserts aFrameList into this list after aPrevSibling (at the beginning if
   * aPrevSibling is null).  If aParent is not null, reparents the newly added
   * frames.  Clears out aFrameList and returns a list slice representing the
   * newly-inserted frames.
   */
  Slice InsertFrames(nsContainerFrame* aParent, nsIFrame* aPrevSibling,
                     nsFrameList&& aFrameList);

  /**
   * Split this list just before the first frame that matches aPredicate,
   * and return a nsFrameList containing all the frames before it. The
   * matched frame and all frames after it stay in this list. If no matched
   * frame exists, all the frames are drained into the returned list, and
   * this list ends up empty.
   *
   * aPredicate should be of this function signature: bool(nsIFrame*).
   */
  template <typename Predicate>
  nsFrameList Split(Predicate&& aPredicate) {
    static_assert(
        std::is_same_v<
            typename mozilla::FunctionTypeTraits<Predicate>::ReturnType,
            bool> &&
            mozilla::FunctionTypeTraits<Predicate>::arity == 1 &&
            std::is_same_v<typename mozilla::FunctionTypeTraits<
                               Predicate>::template ParameterType<0>,
                           nsIFrame*>,
        "aPredicate should be of this function signature: bool(nsIFrame*)");

    for (nsIFrame* f : *this) {
      if (aPredicate(f)) {
        return TakeFramesBefore(f);
      }
    }
    return std::move(*this);
  }

  nsIFrame* FirstChild() const { return mFirstChild; }

  nsIFrame* LastChild() const { return mLastChild; }

  nsIFrame* FrameAt(int32_t aIndex) const;
  int32_t IndexOf(const nsIFrame* aFrame) const;

  bool IsEmpty() const { return nullptr == mFirstChild; }

  bool NotEmpty() const { return nullptr != mFirstChild; }

  /**
   * Return true if aFrame is on this list.
   * @note this method has O(n) time complexity over the length of the list
   * XXXmats: ideally, we should make this function #ifdef DEBUG
   */
  bool ContainsFrame(const nsIFrame* aFrame) const;

  /**
   * Get the number of frames in this list. Note that currently the
   * implementation has O(n) time complexity. Do not call it repeatedly in hot
   * code.
   * XXXmats: ideally, we should make this function #ifdef DEBUG
   */
  int32_t GetLength() const;

  /**
   * If this frame list has only one frame, return that frame.
   * Otherwise, return null.
   */
  nsIFrame* OnlyChild() const {
    if (FirstChild() == LastChild()) {
      return FirstChild();
    }
    return nullptr;
  }

  /**
   * Call SetParent(aParent) for each frame in this list.
   * @param aParent the new parent frame, must be non-null
   */
  void ApplySetParent(nsContainerFrame* aParent) const;

  /**
   * If this frame list is non-empty then append it to aLists as the
   * aListID child list.
   */
  inline void AppendIfNonempty(nsTArray<mozilla::FrameChildList>* aLists,
                               mozilla::FrameChildListID aListID) const {
    if (NotEmpty()) {
      aLists->EmplaceBack(*this, aListID);
    }
  }

  /**
   * Return the frame before this frame in visual order (after Bidi reordering).
   * If aFrame is null, return the last frame in visual order.
   */
  nsIFrame* GetPrevVisualFor(nsIFrame* aFrame) const;

  /**
   * Return the frame after this frame in visual order (after Bidi reordering).
   * If aFrame is null, return the first frame in visual order.
   */
  nsIFrame* GetNextVisualFor(nsIFrame* aFrame) const;

#ifdef DEBUG_FRAME_DUMP
  void List(FILE* out) const;
#endif

  static inline const nsFrameList& EmptyList() { return sEmptyList; };

  /**
   * A class representing a slice of a frame list.
   */
  class Slice {
   public:
    // Implicit on purpose, so that we can easily create Slice from nsFrameList
    // via this impicit constructor.
    MOZ_IMPLICIT Slice(const nsFrameList& aList)
        : mStart(aList.FirstChild()), mEnd(nullptr) {}
    Slice(nsIFrame* aStart, nsIFrame* aEnd) : mStart(aStart), mEnd(aEnd) {}

    void operator delete(void*) = delete;

    iterator begin() const { return iterator(mStart); }
    const_iterator cbegin() const { return begin(); }
    iterator end() const { return iterator(mEnd); }
    const_iterator cend() const { return end(); }

   private:
    // Our starting frame.
    nsIFrame* const mStart;

    // The first frame that is NOT in the slice. May be null.
    nsIFrame* const mEnd;
  };

  template <typename FrameTraversal>
  class Iterator final {
   public:
    // It is disputable whether these type definitions are correct, since
    // operator* doesn't return a reference at all. Also, the iterator_category
    // can be at most std::input_iterator_tag (rather than
    // std::bidrectional_iterator_tag, as it might seem), because it is a
    // stashing iterator. See also, e.g.,
    // https://stackoverflow.com/questions/50909701/what-should-be-iterator-category-for-a-stashing-iterator
    using value_type = nsIFrame* const;
    using pointer = value_type*;
    using reference = value_type&;
    using difference_type = ptrdiff_t;
    using iterator_category = std::input_iterator_tag;

    explicit constexpr Iterator(nsIFrame* aCurrent) : mCurrent(aCurrent) {}

    nsIFrame* operator*() const { return mCurrent; }

    Iterator& operator++() {
      mCurrent = FrameTraversal::Next(mCurrent);
      return *this;
    }
    Iterator& operator--() {
      mCurrent = FrameTraversal::Prev(mCurrent);
      return *this;
    }

    Iterator operator++(int) {
      auto ret = *this;
      ++*this;
      return ret;
    }
    Iterator operator--(int) {
      auto ret = *this;
      --*this;
      return ret;
    }

    bool operator==(const Iterator<FrameTraversal>& aOther) const = default;

   private:
    nsIFrame* mCurrent;
  };

  iterator begin() const { return iterator(mFirstChild); }
  const_iterator cbegin() const { return begin(); }
  iterator end() const { return iterator(nullptr); }
  const_iterator cend() const { return end(); }
  reverse_iterator rbegin() const { return reverse_iterator(mLastChild); }
  const_reverse_iterator crbegin() const { return rbegin(); }
  reverse_iterator rend() const { return reverse_iterator(nullptr); }
  const_reverse_iterator crend() const { return rend(); }

 private:
  static const nsFrameList sEmptyList;

#ifdef DEBUG_FRAME_LIST
  void VerifyList() const;
#else
  void VerifyList() const {}
#endif

 protected:
  /**
   * Disconnect aFrame from its siblings.  This must only be called if aFrame
   * is NOT the first or last sibling, because otherwise its nsFrameList will
   * have a stale mFirst/LastChild pointer.  This precondition is asserted.
   * This function is O(1).
   */
  static void UnhookFrameFromSiblings(nsIFrame* aFrame);

  nsIFrame* mFirstChild;
  nsIFrame* mLastChild;
};

namespace mozilla {

#ifdef DEBUG_FRAME_DUMP
extern const char* ChildListName(FrameChildListID aListID);
#endif

using FrameChildListIDs = EnumSet<FrameChildListID>;

class FrameChildList {
 public:
  FrameChildList(const nsFrameList& aList, FrameChildListID aID)
      : mList(aList.Clone()), mID(aID) {}
  nsFrameList mList;
  FrameChildListID mID;
};

/**
 * Simple "auto_ptr" for nsFrameLists allocated from the shell arena.
 * The frame list given to the constructor will be deallocated (if non-null)
 * in the destructor.  The frame list must then be empty.
 */
class MOZ_RAII AutoFrameListPtr final {
 public:
  AutoFrameListPtr(nsPresContext* aPresContext, nsFrameList* aFrameList)
      : mPresContext(aPresContext), mFrameList(aFrameList) {}
  ~AutoFrameListPtr();
  operator nsFrameList*() const { return mFrameList; }
  nsFrameList* operator->() const { return mFrameList; }

 private:
  nsPresContext* mPresContext;
  nsFrameList* mFrameList;
};

}  // namespace mozilla

#endif /* nsFrameList_h_ */
