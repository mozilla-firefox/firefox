/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* base class #1 for rendering objects that have child lists */

#ifndef nsContainerFrame_h_
#define nsContainerFrame_h_

#include "LayoutConstants.h"
#include "mozilla/Attributes.h"
#include "nsFrameList.h"
#include "nsISelectionDisplay.h"
#include "nsLineBox.h"
#include "nsSplittableFrame.h"
#include "nsTHashSet.h"

class nsOverflowContinuationTracker;

namespace mozilla {
class PresShell;
struct StylePositionArea;
}  // namespace mozilla

// Some macros for container classes to do sanity checking on
// width/height/x/y values computed during reflow.
// NOTE: AppUnitsPerCSSPixel value hardwired here to remove the
// dependency on nsDeviceContext.h.  It doesn't matter if it's a
// little off.
#ifdef DEBUG
// 10 million pixels, converted to app units. Note that this a bit larger
// than 1/4 of nscoord_MAX. So, if any content gets to be this large, we're
// definitely in danger of grazing up against nscoord_MAX; hence, it's ABSURD.
#  define ABSURD_COORD (10000000 * 60)
#  define ABSURD_SIZE(_x) (((_x) < -ABSURD_COORD) || ((_x) > ABSURD_COORD))
#endif

/**
 * Implementation of a container frame.
 */
class nsContainerFrame : public nsSplittableFrame {
 public:
  NS_DECL_ABSTRACT_FRAME(nsContainerFrame)
  NS_DECL_QUERYFRAME_TARGET(nsContainerFrame)
  NS_DECL_QUERYFRAME

  // nsIFrame overrides
  nsContainerFrame* GetContentInsertionFrame() override { return this; }

  const nsFrameList& GetChildList(ChildListID aList) const override;
  void GetChildLists(nsTArray<ChildList>* aLists) const override;
  void Destroy(DestroyContext&) override;

  void ChildIsDirty(nsIFrame* aChild) override;

  FrameSearchResult PeekOffsetNoAmount(bool aForward,
                                       int32_t* aOffset) override;
  FrameSearchResult PeekOffsetCharacter(
      bool aForward, int32_t* aOffset,
      PeekOffsetCharacterOptions aOptions =
          PeekOffsetCharacterOptions()) override;

#ifdef DEBUG_FRAME_DUMP
  void List(FILE* out = stderr, const char* aPrefix = "",
            ListFlags aFlags = ListFlags()) const override;
  void ListWithMatchedRules(FILE* out = stderr,
                            const char* aPrefix = "") const override;
  void ListChildLists(FILE* aOut, const char* aPrefix, ListFlags aFlags,
                      ChildListIDs aSkippedListIDs) const;
  virtual void ExtraContainerFrameInfo(nsACString& aTo,
                                       bool aListOnlyDeterministic) const;
#endif

  // nsContainerFrame methods

  /**
   * Called to set the initial list of frames. This happens after the frame
   * has been initialized.
   *
   * This is only called once for a given child list, and won't be called
   * at all for child lists with no initial list of frames.
   *
   * @param   aListID the child list identifier.
   * @param   aChildList list of child frames. Each of the frames has its
   *            NS_FRAME_IS_DIRTY bit set.  Must not be empty.
   *            This method cannot handle ChildListID::Absolute frames.
   * @see     #Init()
   */
  virtual void SetInitialChildList(ChildListID aListID,
                                   nsFrameList&& aChildList);

  /**
   * This method is responsible for appending frames to the frame
   * list.  The implementation should append the frames to the specified
   * child list and then generate a reflow command.
   *
   * @param   aListID the child list identifier.
   * @param   aFrameList list of child frames to append. Each of the frames has
   *            its NS_FRAME_IS_DIRTY bit set.  Must not be empty.
   */
  virtual void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList);

  /**
   * This method is responsible for inserting frames into the frame
   * list.  The implementation should insert the new frames into the specified
   * child list and then generate a reflow command.
   *
   * @param   aListID the child list identifier.
   * @param   aPrevFrame the frame to insert frames <b>after</b>
   * @param   aPrevFrameLine (optional) if present (i.e., not null), the line
   *            box that aPrevFrame is part of.
   * @param   aFrameList list of child frames to insert <b>after</b> aPrevFrame.
   *            Each of the frames has its NS_FRAME_IS_DIRTY bit set
   */
  virtual void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                            const nsLineList::iterator* aPrevFrameLine,
                            nsFrameList&& aFrameList);

  /**
   * This method is responsible for removing a frame in the frame
   * list.  The implementation should do something with the removed frame
   * and then generate a reflow command. The implementation is responsible
   * for destroying the frame (the caller mustn't destroy it).
   */
  virtual void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*);

  /**
   * Helper method to create next-in-flows if necessary. If aFrame
   * already has a next-in-flow then this method does
   * nothing. Otherwise, a new continuation frame is created and
   * linked into the flow. In addition, the new frame is inserted
   * into the principal child list after aFrame.
   * @note calling this method on a block frame is illegal. Use
   * nsBlockFrame::CreateContinuationFor() instead.
   * @return the next-in-flow <b>if and only if</b> one is created. If
   *         a next-in-flow already exists, nullptr will be returned.
   */
  nsIFrame* CreateNextInFlow(nsIFrame* aFrame);

  /**
   * Delete aNextInFlow and its next-in-flows.
   * @param aDeletingEmptyFrames if set, then the reflow for aNextInFlow's
   * content was complete before aNextInFlow, so aNextInFlow and its
   * next-in-flows no longer map any real content.
   */
  virtual void DeleteNextInFlowChild(DestroyContext&, nsIFrame* aNextInFlow,
                                     bool aDeletingEmptyFrames);

  /**
   * Reparent aFrame from aOldParent to aNewParent.
   */
  static void ReparentFrame(nsIFrame* aFrame, nsContainerFrame* aOldParent,
                            nsContainerFrame* aNewParent);

  /**
   * Reparent all the frames in aFrameList from aOldParent to aNewParent.
   *
   * Note: Reparenting a large frame list can be have huge performance impact.
   * For example, instead of using this method, nsInlineFrame uses a "lazy
   * reparenting" technique that it reparents a child frame just before
   * reflowing the child. (See InlineReflowInput::mSetParentDuringReflow.)
   */
  static void ReparentFrames(nsFrameList& aFrameList,
                             nsContainerFrame* aOldParent,
                             nsContainerFrame* aNewParent);

  /**
   * Converts the minimum and maximum sizes given in inner window app units to
   * outer window device pixel sizes and assigns these constraints to the
   * widget.
   *
   * @param aPresContext pres context
   * @param aWidget widget for this frame
   * @param minimum size of the window in app units
   * @param maxmimum size of the window in app units
   */
  static void SetSizeConstraints(nsPresContext* aPresContext,
                                 nsIWidget* aWidget, const nsSize& aMinSize,
                                 const nsSize& aMaxSize);

  /**
   * Helper for calculating intrinsic inline size for inline containers.
   *
   * @param aData the intrinsic inline size data, either an InlineMinISizeData
   *  or an InlinePrefISizeData
   * @param aHandleChildren a callback function invoked for each in-flow
   *  continuation, with the continuation frame and the intrinsic inline size
   *  data passed into it.
   */
  template <typename ISizeData, typename F>
  void DoInlineIntrinsicISize(ISizeData* aData, F& aHandleChildren);

  void DoInlineMinISize(const mozilla::IntrinsicSizeInput& aInput,
                        InlineMinISizeData* aData);
  void DoInlinePrefISize(const mozilla::IntrinsicSizeInput& aInput,
                         InlinePrefISizeData* aData);

  /**
   * This is the CSS block concept of computing 'auto' widths, which most
   * classes derived from nsContainerFrame want.
   */
  virtual mozilla::LogicalSize ComputeAutoSize(
      const SizeComputationInput& aSizingInput, mozilla::WritingMode aWM,
      const mozilla::LogicalSize& aCBSize, nscoord aAvailableISize,
      const mozilla::LogicalSize& aMargin,
      const mozilla::LogicalSize& aBorderPadding,
      const mozilla::StyleSizeOverrides& aSizeOverrides,
      mozilla::ComputeSizeFlags aFlags) override;

  /**
   * Positions aKidFrame and its view (if requested), and then calls Reflow().
   * If the reflow status after reflowing the child is FullyComplete then any
   * next-in-flows are deleted using DeleteNextInFlowChild().
   *
   * @param aReflowInput the reflow input for aKidFrame.
   * @param aWM aPos's writing-mode (any writing mode will do).
   * @param aPos Position of the aKidFrame to be moved, in terms of aWM.
   * @param aContainerSize Size of the border-box of the containing frame.
   *
   * Note: If ReflowChildFlags::NoMoveFrame is requested, both aPos and
   * aContainerSize are ignored.
   */
  void ReflowChild(nsIFrame* aKidFrame, nsPresContext* aPresContext,
                   ReflowOutput& aDesiredSize, const ReflowInput& aReflowInput,
                   const mozilla::WritingMode& aWM,
                   const mozilla::LogicalPoint& aPos,
                   const nsSize& aContainerSize, ReflowChildFlags aFlags,
                   nsReflowStatus& aStatus,
                   nsOverflowContinuationTracker* aTracker = nullptr);

  /**
   * The second half of frame reflow. Does the following:
   * - sets the frame's bounds
   * - sizes and positions (if requested) the frame's view. If the frame's final
   *   position differs from the current position and the frame itself does not
   *   have a view, then any child frames with views are positioned so they stay
   *   in sync
   * - sets the view's visibility, opacity, content transparency, and clip
   * - invoked the DidReflow() function
   *
   * @param aReflowInput the reflow input for aKidFrame.
   * @param aWM aPos's writing-mode (any writing mode will do).
   * @param aPos Position of the aKidFrame to be moved, in terms of aWM.
   * @param aContainerSize Size of the border-box of the containing frame.
   *
   * Note: If ReflowChildFlags::NoMoveFrame is requested, both aPos and
   * aContainerSize are ignored unless
   * ReflowChildFlags::ApplyRelativePositioning is requested.
   */
  static void FinishReflowChild(
      nsIFrame* aKidFrame, nsPresContext* aPresContext,
      const ReflowOutput& aDesiredSize, const ReflowInput* aReflowInput,
      const mozilla::WritingMode& aWM, const mozilla::LogicalPoint& aPos,
      const nsSize& aContainerSize, ReflowChildFlags aFlags);

  // XXX temporary: hold on to a copy of the old physical versions of
  //    ReflowChild and FinishReflowChild so that we can convert callers
  //    incrementally.
  void ReflowChild(nsIFrame* aKidFrame, nsPresContext* aPresContext,
                   ReflowOutput& aDesiredSize, const ReflowInput& aReflowInput,
                   nscoord aX, nscoord aY, ReflowChildFlags aFlags,
                   nsReflowStatus& aStatus,
                   nsOverflowContinuationTracker* aTracker = nullptr);

  static void FinishReflowChild(nsIFrame* aKidFrame,
                                nsPresContext* aPresContext,
                                const ReflowOutput& aDesiredSize,
                                const ReflowInput* aReflowInput, nscoord aX,
                                nscoord aY, ReflowChildFlags aFlags);

  /**
   * Let the absolutely positioned containing block reflow any absolutely
   * positioned child frames that need to be reflowed.
   *
   * @param aStatus The reflow statuses of any reflowed absolute children will
   * be merged into aStatus; aside from that, this method won't modify aStatus.
   */
  void ReflowAbsoluteFrames(nsPresContext* aPresContext,
                            ReflowOutput& aDesiredSize,
                            const ReflowInput& aReflowInput,
                            nsReflowStatus& aStatus);

  /**
   * A convenience method to call ReflowAbsoluteFrames() and
   * FinishAndStoreOverflow().
   */
  void FinishReflowWithAbsoluteFrames(nsPresContext* aPresContext,
                                      ReflowOutput& aDesiredSize,
                                      const ReflowInput& aReflowInput,
                                      nsReflowStatus& aStatus);

  // ==========================================================================
  /* Overflow containers are continuation frames that hold overflow. They
   * are created when the frame runs out of computed block-size, but still has
   * too much content to fit in the AvailableBSize. The parent creates a
   * continuation as usual, but marks it as NS_FRAME_IS_OVERFLOW_CONTAINER
   * and adds it to its next-in-flow's overflow container list, either by
   * adding it directly or by putting it in its own excess overflow containers
   * list (to be drained by the next-in-flow when it calls
   * ReflowOverflowContainerChildren). The parent continues reflow as if
   * the frame was complete once it ran out of computed block-size, but returns
   * a reflow status with either IsIncomplete() or IsOverflowIncomplete() equal
   * to true to request a next-in-flow. The parent's next-in-flow is then
   * responsible for calling ReflowOverflowContainerChildren to (drain and)
   * reflow these overflow continuations. Overflow containers do not affect
   * other frames' size or position during reflow (but do affect their
   * parent's overflow area).
   *
   * Overflow container continuations are different from normal continuations
   * in that
   *   - more than one child of the frame can have its next-in-flow broken
   *     off and pushed into the frame's next-in-flow
   *   - new continuations may need to be spliced into the middle of the list
   *     or deleted continuations slipped out
   *     e.g. A, B, C are all fixed-size containers on one page, all have
   *      overflow beyond AvailableBSize, and content is dynamically added
   *      and removed from B
   * As a result, it is not possible to simply prepend the new continuations
   * to the old list as with the OverflowProperty mechanism. To avoid
   * complicated list splicing, the code assumes only one overflow containers
   * list exists for a given frame: either its own OverflowContainersProperty
   * or its prev-in-flow's ExcessOverflowContainersProperty, not both.
   *
   * The nsOverflowContinuationTracker helper class should be used for tracking
   * overflow containers and adding them to the appropriate list.
   * See nsBlockFrame::Reflow for a sample implementation.
   *
   * For more information, see
   * https://firefox-source-docs.mozilla.org/layout/LayoutOverview.html#fragmentation
   *
   * Note that Flex/GridContainerFrame doesn't use nsOverflowContinuationTracker
   * so the above doesn't apply.  Flex/Grid containers may have items that
   * aren't in document order between fragments, due to the 'order' property,
   * but they do maintain the invariant that children in the same nsFrameList
   * are in document order.  This means that when pushing/pulling items or
   * merging lists, the result needs to be sorted to restore the order.
   * However, given that lists are individually sorted, it's a simple merge
   * operation of the two lists to make the result sorted.
   * DrainExcessOverflowContainersList takes a merging function to perform that
   * operation.  (By "document order" here we mean normal frame tree order,
   * which is approximately flattened DOM tree order.)
   */

  friend class nsOverflowContinuationTracker;

  typedef void (*ChildFrameMerger)(nsFrameList& aDest, nsFrameList& aSrc,
                                   nsContainerFrame* aParent);
  static inline void DefaultChildFrameMerge(nsFrameList& aDest,
                                            nsFrameList& aSrc,
                                            nsContainerFrame* aParent) {
    aDest.AppendFrames(nullptr, std::move(aSrc));
  }

  /**
   * Reflow overflow container children. They are invisible to normal reflow
   * (i.e. don't affect sizing or placement of other children) and inherit
   * width and horizontal position from their prev-in-flow.
   *
   * This method
   *   1. Pulls excess overflow containers from the prev-in-flow and adds
   *      them to our overflow container list
   *   2. Reflows all our overflow container kids
   *   3. Expands aOverflowRect as necessary to accomodate these children.
   *   4. Sets aStatus's mOverflowIncomplete flag (along with
   *      mNextInFlowNeedsReflow as necessary) if any overflow children
   *      are incomplete and
   *   5. Prepends a list of their continuations to our excess overflow
   *      container list, to be drained into our next-in-flow when it is
   *      reflowed.
   *
   * The caller is responsible for tracking any new overflow container
   * continuations it makes, removing them from its child list, and
   * making sure they are stored properly in the overflow container lists.
   * The nsOverflowContinuationTracker helper class should be used for this.
   *
   * @param aFlags is passed through to ReflowChild
   * @param aMergeFunc is passed to DrainExcessOverflowContainersList
   * @param aContainerSize is used only for converting logical coordinate to
   *        physical coordinate. If a tentative container size is used, caller
   *        may need to adjust the position of our overflow container children
   *        once the real size is known if our writing mode is vertical-rl.
   */
  void ReflowOverflowContainerChildren(
      nsPresContext* aPresContext, const ReflowInput& aReflowInput,
      mozilla::OverflowAreas& aOverflowRects, ReflowChildFlags aFlags,
      nsReflowStatus& aStatus,
      ChildFrameMerger aMergeFunc = DefaultChildFrameMerge,
      Maybe<nsSize> aContainerSize = Nothing());

  /**
   * Move any frames on our overflow list to the end of our principal list.
   * @return true if there were any overflow frames
   */
  virtual bool DrainSelfOverflowList() override;

  /**
   * Move all frames on our prev-in-flow's and our own ExcessOverflowContainers
   * lists to our OverflowContainers list.  If there are frames on multiple
   * lists they are merged using aMergeFunc.
   * @return a pointer to our OverflowContainers list, if any
   */
  nsFrameList* DrainExcessOverflowContainersList(
      ChildFrameMerger aMergeFunc = DefaultChildFrameMerge);

  /**
   * Removes aChild without destroying it and without requesting reflow.
   * Continuations are not affected.  Checks the principal and overflow lists,
   * and also the [excess] overflow containers lists if the frame bit
   * NS_FRAME_IS_OVERFLOW_CONTAINER is set.  It does not check any other lists.
   * aChild must be in one of the above mentioned lists, or an assertion is
   * triggered.
   *
   * Note: This method can destroy either overflow list or [excess] overflow
   * containers list if aChild is the only child in the list. Any pointer to the
   * list obtained prior to calling this method shouldn't be used.
   */
  virtual void StealFrame(nsIFrame* aChild);

  /**
   * Removes the next-siblings of aChild without destroying them and without
   * requesting reflow. Checks the principal and overflow lists (not
   * overflow containers / excess overflow containers). Does not check any
   * other auxiliary lists.
   * @param aChild a child frame or nullptr
   * @return If aChild is non-null, the next-siblings of aChild, if any.
   *         If aChild is null, all child frames on the principal list, if any.
   */
  nsFrameList StealFramesAfter(nsIFrame* aChild);

  /**
   * Add overflow containers to the display list
   */
  void DisplayOverflowContainers(nsDisplayListBuilder* aBuilder,
                                 const nsDisplayListSet& aLists);

  /**
   * Add absolute frames to the display list that should not be built via their
   * placeholder. This includes:
   * - Pushed out-of-flow frames (NS_FRAME_IS_PUSHED_OUT_OF_FLOW), whose
   *   containing block is not an ancestor of the placeholder.
   * - Under a transformed absolute containing block, abspos frames whose
   *   placeholder is in a different continuation's subtree, so that they end up
   *   in the correct fragment's nsDisplayTransform.
   */
  void DisplayAbsoluteFramesNotBuiltByPlaceholder(
      nsDisplayListBuilder* aBuilder, const nsDisplayListSet& aLists);

  /**
   * Builds display lists for the children. The background
   * of each child is placed in the Content() list (suitable for inline
   * children and other elements that behave like inlines,
   * but not for in-flow block children of blocks).  DOES NOT
   * paint the background/borders/outline of this frame. This should
   * probably be avoided and eventually removed. It's currently here
   * to emulate what nsContainerFrame::Paint did.
   */
  virtual void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                const nsDisplayListSet& aLists) override;

  /**
   * Returns a CSS Box Alignment constant which the caller can use to align
   * the absolutely-positioned child (whose ReflowInput is aChildRI) within
   * a CSS Box Alignment area associated with this container. Used for
   * computing the static position of an absolutely positioned box.
   *
   * The lower 8 bits of the returned value are guaranteed to form a valid
   * argument for CSSAlignUtils::AlignJustifySelf(). (The upper 8 bits may
   * encode an <overflow-position>.)
   *
   * @param aChildRI A ReflowInput for the positioned child frame that's being
   *                 aligned.
   * @param aLogicalAxis The axis (of this container frame) in which the caller
   *                     would like to align the child frame.
   */
  virtual mozilla::StyleAlignFlags CSSAlignmentForAbsPosChild(
      const ReflowInput& aChildRI, mozilla::LogicalAxis aLogicalAxis) const;

  /**
   * Default implementation of `CSSAlignmentForAbsPosChild`, where we treat
   * this frame as a plain absolute containing block instead of depending
   * on its type (By overriding `CSSAlignmentForAbsPosChild`).
   */
  mozilla::StyleAlignFlags CSSAlignmentForAbsPosChildWithinContainingBlock(
      const SizeComputationInput& aSizingInput,
      mozilla::LogicalAxis aLogicalAxis,
      const mozilla::StylePositionArea& aResolvedPositionArea,
      const mozilla::LogicalSize& aContainingBlockSize) const;

#define NS_DECLARE_FRAME_PROPERTY_FRAMELIST(prop) \
  NS_DECLARE_FRAME_PROPERTY_WITH_DTOR_NEVER_CALLED(prop, nsFrameList)

  using FrameListPropertyDescriptor =
      mozilla::FrameProperties::Descriptor<nsFrameList>;

  NS_DECLARE_FRAME_PROPERTY_FRAMELIST(OverflowProperty)
  NS_DECLARE_FRAME_PROPERTY_FRAMELIST(OverflowContainersProperty)
  NS_DECLARE_FRAME_PROPERTY_FRAMELIST(ExcessOverflowContainersProperty)

  // Only really used on nsBlockFrame instances, but the caller thinks it could
  // have arbitrary nsContainerFrames.
  NS_DECLARE_FRAME_PROPERTY_WITHOUT_DTOR(FirstLetterProperty, nsIFrame)

  void SetHasFirstLetterChild() {
    AddStateBits(NS_FRAME_HAS_FIRST_LETTER_CHILD);
  }

  void ClearHasFirstLetterChild() {
    RemoveStateBits(NS_FRAME_HAS_FIRST_LETTER_CHILD);
  }

#ifdef DEBUG
  // Use this to suppress the ABSURD_SIZE assertions.
  NS_DECLARE_FRAME_PROPERTY_SMALL_VALUE(DebugReflowingWithInfiniteISize, bool)
  bool IsAbsurdSizeAssertSuppressed() const {
    return GetProperty(DebugReflowingWithInfiniteISize());
  }
#endif

  // Incorporate the child overflow areas into aOverflowAreas.
  // If the child does not have a overflow, use the child area.
  void ConsiderChildOverflow(mozilla::OverflowAreas& aOverflowAreas,
                             nsIFrame* aChildFrame,
                             mozilla::OverflowAreaUnionFlags aFlags =
                                 mozilla::OverflowAreaUnionFlags::None);

 protected:
  nsContainerFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                   ClassID aID)
      : nsSplittableFrame(aStyle, aPresContext, aID) {}

  ~nsContainerFrame();

  /**
   * Helper for DestroyFrom. DestroyAbsoluteFrames is called before
   * destroying frames on lists that can contain placeholders.
   * Derived classes must do that too, if they destroy such frame lists.
   * See nsBlockFrame::DestroyFrom for an example.
   */
  void DestroyAbsoluteFrames(DestroyContext&);

  /**
   * Helper for StealFrame.  Returns true if aChild was removed from its list.
   */
  bool MaybeStealOverflowContainerFrame(nsIFrame* aChild);

  /**
   * Builds a display list for non-block children that behave like
   * inlines. This puts the background of each child into the
   * Content() list (suitable for inline children but not for
   * in-flow block children of blocks).
   * @param aForcePseudoStack forces each child into a pseudo-stacking-context
   * so its background and all other display items (except for positioned
   * display items) go into the Content() list.
   */
  void BuildDisplayListForNonBlockChildren(nsDisplayListBuilder* aBuilder,
                                           const nsDisplayListSet& aLists,
                                           DisplayChildFlags aFlags = {});

  /**
   * A version of BuildDisplayList that use DisplayChildFlag::Inline.
   * Intended as a convenience for derived classes.
   */
  void BuildDisplayListForInline(nsDisplayListBuilder* aBuilder,
                                 const nsDisplayListSet& aLists) {
    DisplayBorderBackgroundOutline(aBuilder, aLists);
    BuildDisplayListForNonBlockChildren(aBuilder, aLists,
                                        DisplayChildFlag::Inline);
  }

  // ==========================================================================
  /* Overflow Frames are frames that did not fit and must be pulled by
   * our next-in-flow during its reflow. (The same concept for overflow
   * containers is called "excess frames". We should probably make the
   * names match.)
   */

  /**
   * Get the frames on the overflow list, overflow containers list, or excess
   * overflow containers list. Can return null if there are no frames in the
   * list.
   *
   * The caller does NOT take ownership of the list; it's still owned by this
   * frame. A non-null return value indicates that the list is non-empty.
   */
  [[nodiscard]] nsFrameList* GetOverflowFrames() const {
    nsFrameList* list = GetProperty(OverflowProperty());
    NS_ASSERTION(!list || !list->IsEmpty(), "Unexpected empty overflow list");
    return list;
  }
  [[nodiscard]] nsFrameList* GetOverflowContainers() const {
    nsFrameList* list = GetProperty(OverflowContainersProperty());
    NS_ASSERTION(!list || !list->IsEmpty(),
                 "Unexpected empty overflow containers list");
    return list;
  }
  [[nodiscard]] nsFrameList* GetExcessOverflowContainers() const {
    nsFrameList* list = GetProperty(ExcessOverflowContainersProperty());
    NS_ASSERTION(!list || !list->IsEmpty(),
                 "Unexpected empty overflow containers list");
    return list;
  }

  /**
   * Same as the Get methods above, but also remove and the property from this
   * frame.
   *
   * The caller is responsible for deleting nsFrameList and either passing
   * ownership of the frames to someone else or destroying the frames. A
   * non-null return value indicates that the list is non-empty. The recommended
   * way to use this function it to assign its return value into an
   * AutoFrameListPtr.
   */
  [[nodiscard]] nsFrameList* StealOverflowFrames() {
    nsFrameList* list = TakeProperty(OverflowProperty());
    NS_ASSERTION(!list || !list->IsEmpty(), "Unexpected empty overflow list");
    return list;
  }
  [[nodiscard]] nsFrameList* StealOverflowContainers() {
    nsFrameList* list = TakeProperty(OverflowContainersProperty());
    NS_ASSERTION(!list || !list->IsEmpty(), "Unexpected empty overflow list");
    return list;
  }
  [[nodiscard]] nsFrameList* StealExcessOverflowContainers() {
    nsFrameList* list = TakeProperty(ExcessOverflowContainersProperty());
    NS_ASSERTION(!list || !list->IsEmpty(), "Unexpected empty overflow list");
    return list;
  }

  /**
   * Set the overflow list, overflow containers list, or excess overflow
   * containers list. The argument must be a *non-empty* list.
   *
   * After this operation, the argument becomes an empty list.
   *
   * @return the frame list associated with the property.
   */
  nsFrameList* SetOverflowFrames(nsFrameList&& aOverflowFrames) {
    MOZ_ASSERT(aOverflowFrames.NotEmpty(), "Shouldn't be called");
    auto* list = new nsFrameList(std::move(aOverflowFrames));
    SetProperty(OverflowProperty(), list);
    return list;
  }
  nsFrameList* SetOverflowContainers(nsFrameList&& aOverflowContainers) {
    MOZ_ASSERT(aOverflowContainers.NotEmpty(), "Shouldn't set an empty list!");
    MOZ_ASSERT(!GetProperty(OverflowContainersProperty()),
               "Shouldn't override existing list!");
    MOZ_ASSERT(CanContainOverflowContainers(),
               "This type of frame can't have overflow containers!");
    auto* list = new nsFrameList(std::move(aOverflowContainers));
    SetProperty(OverflowContainersProperty(), list);
    return list;
  }
  nsFrameList* SetExcessOverflowContainers(
      nsFrameList&& aExcessOverflowContainers) {
    MOZ_ASSERT(aExcessOverflowContainers.NotEmpty(),
               "Shouldn't set an empty list!");
    MOZ_ASSERT(!GetProperty(ExcessOverflowContainersProperty()),
               "Shouldn't override existing list!");
    MOZ_ASSERT(CanContainOverflowContainers(),
               "This type of frame can't have overflow containers!");
    auto* list = new nsFrameList(std::move(aExcessOverflowContainers));
    SetProperty(ExcessOverflowContainersProperty(), list);
    return list;
  }

  /**
   * Destroy the overflow list, overflow containers list, or excess overflow
   * containers list.
   *
   * The list to be destroyed must be empty. That is, the caller is responsible
   * for either passing ownership of the frames to someone else or destroying
   * the frames before calling these methods.
   */
  void DestroyOverflowList() {
    nsFrameList* list = TakeProperty(OverflowProperty());
    MOZ_ASSERT(list && list->IsEmpty());
    list->Delete();
  }
  void DestroyOverflowContainers() {
    nsFrameList* list = TakeProperty(OverflowContainersProperty());
    MOZ_ASSERT(list && list->IsEmpty());
    list->Delete();
  }
  void DestroyExcessOverflowContainers() {
    nsFrameList* list = TakeProperty(ExcessOverflowContainersProperty());
    MOZ_ASSERT(list && list->IsEmpty());
    list->Delete();
  }

  /**
   * Moves any frames on both the prev-in-flow's overflow list and the
   * receiver's overflow to the receiver's child list.
   *
   * Resets the overlist pointers to nullptr, and updates the receiver's child
   * count and content mapping.
   *
   * @return true if any frames were moved and false otherwise
   */
  bool MoveOverflowToChildList();

  /**
   * Merge a sorted frame list into our overflow list. aList becomes empty after
   * this call.
   */
  void MergeSortedOverflow(nsFrameList& aList);

  /**
   * Merge a sorted frame list into our excess overflow containers list. aList
   * becomes empty after this call.
   */
  void MergeSortedExcessOverflowContainers(nsFrameList& aList);

  /**
   * Moves all frames from aSrc into aDest such that the resulting aDest
   * is still sorted in document content order and continuation order. aSrc
   * becomes empty after this call.
   *
   * Precondition: both |aSrc| and |aDest| must be sorted to begin with.
   * @param aCommonAncestor a hint for nsLayoutUtils::CompareTreePosition
   */
  static void MergeSortedFrameLists(nsFrameList& aDest, nsFrameList& aSrc,
                                    nsIContent* aCommonAncestor);

  /**
   * This is intended to be used as a ChildFrameMerger argument for
   * ReflowOverflowContainerChildren() and DrainExcessOverflowContainersList().
   */
  static inline void MergeSortedFrameListsFor(nsFrameList& aDest,
                                              nsFrameList& aSrc,
                                              nsContainerFrame* aParent) {
    MergeSortedFrameLists(aDest, aSrc, aParent->GetContent());
  }

  /**
   * Basically same as MoveOverflowToChildList, except that this is for
   * handling inline children where children of prev-in-flow can be
   * pushed to overflow list even if a next-in-flow exists.
   *
   * @param aLineContainer the line container of the current frame.
   *
   * @return true if any frames were moved and false otherwise
   */
  bool MoveInlineOverflowToChildList(nsIFrame* aLineContainer);

  /**
   * Push aFromChild and its next siblings to the overflow list.
   *
   * @param aFromChild the first child frame to push. It is disconnected
   *          from aPrevSibling
   * @param aPrevSibling aFrameChild's previous sibling. Must not be null.
   *          It's an error to push a parent's first child frame.
   */
  void PushChildrenToOverflow(nsIFrame* aFromChild, nsIFrame* aPrevSibling);

  /**
   * Iterate our children in our principal child list in the normal document
   * order, and append them (or their next-in-flows) to either our overflow list
   * or excess overflow container list according to their presence in
   * aPushedItems, aIncompleteItems, or aOverflowIncompleteItems.
   *
   * Note: This method is only intended for Grid / Flex containers.
   * aPushedItems, aIncompleteItems, and aOverflowIncompleteItems are expected
   * to contain only Grid / Flex items. That is, they should contain only
   * in-flow children.
   *
   * @return true if any items are moved; false otherwise.
   */
  using FrameHashtable = nsTHashSet<nsIFrame*>;
  bool PushIncompleteChildren(const FrameHashtable& aPushedItems,
                              const FrameHashtable& aIncompleteItems,
                              const FrameHashtable& aOverflowIncompleteItems);

  /**
   * Prepare our child lists so that they are ready to reflow by the following
   * operations:
   *
   * - Merge overflow list from our prev-in-flow into our principal child list.
   * - Merge our own overflow list into our principal child list,
   * - Push any child's next-in-flows in our principal child list to our
   *   overflow list.
   * - Pull up any first-in-flow child we might have pushed from our
   *   next-in-flows.
   */
  void NormalizeChildLists();

  /**
   * Helper to implement AppendFrames / InsertFrames for flex / grid
   * containers.
   */
  void NoteNewChildren(ChildListID aListID, const nsFrameList& aFrameList);

  /**
   * Helper to implement DrainSelfOverflowList() for flex / grid containers.
   */
  bool DrainAndMergeSelfOverflowList();

  /**
   * Helper to find the first non-anonymous-box frame in the subtree rooted at
   * aFrame.
   */
  static nsIFrame* GetFirstNonAnonBoxInSubtree(nsIFrame* aFrame);

  /**
   * Reparent floats whose placeholders are inline descendants of aFrame from
   * whatever block they're currently parented by to aOurBlock.
   * @param aReparentSiblings if this is true, we follow aFrame's
   * GetNextSibling chain reparenting them all
   */
  static void ReparentFloatsForInlineChild(nsIFrame* aOurBlock,
                                           nsIFrame* aFrame,
                                           bool aReparentSiblings);

  /**
   * Try to remove aChildToRemove from the frame list stored in aProp.
   * If aChildToRemove was removed from the aProp list and that list became
   * empty, then aProp is removed from this frame and deleted.
   * @note if aChildToRemove isn't on the aProp frame list, it might still be
   * removed from whatever list it happens to be on, so use this method
   * carefully.  This method is primarily meant for removing frames from the
   * [Excess]OverflowContainers lists.
   * @return true if aChildToRemove was removed from some list
   */
  bool TryRemoveFrame(FrameListPropertyDescriptor aProp,
                      nsIFrame* aChildToRemove);

  // ==========================================================================
  /*
   * Convenience methods for traversing continuations
   */

  struct ContinuationTraversingState {
    nsContainerFrame* mNextInFlow;
    explicit ContinuationTraversingState(nsContainerFrame* aFrame)
        : mNextInFlow(static_cast<nsContainerFrame*>(aFrame->GetNextInFlow())) {
    }
  };

  /**
   * Find the first frame that is a child of this frame's next-in-flows,
   * considering both their principal child lists and overflow lists.
   */
  nsIFrame* GetNextInFlowChild(ContinuationTraversingState& aState,
                               bool* aIsInOverflow = nullptr);

  /**
   * Remove the result of GetNextInFlowChild from its current parent and
   * append it to this frame's principal child list.
   */
  nsIFrame* PullNextInFlowChild(ContinuationTraversingState& aState);

  /**
   * Safely destroy the frames on the nsFrameList stored on aProp for this
   * frame then remove the property and delete the frame list.
   * Nothing happens if the property doesn't exist.
   */
  void SafelyDestroyFrameListProp(DestroyContext&,
                                  mozilla::PresShell* aPresShell,
                                  FrameListPropertyDescriptor aProp);

  // ==========================================================================

  // Helper used by Progress and Meter frames. Returns true if the bar should
  // be rendered vertically, based on writing-mode and -moz-orient properties.
  bool ResolvedOrientationIsVertical() const;

  /**
   * Calculate the used values for 'width' and 'height' for a replaced element.
   *   http://www.w3.org/TR/CSS21/visudet.html#min-max-widths
   *
   * @param aAspectRatio the aspect ratio calculated by GetAspectRatio().
   */
  mozilla::LogicalSize ComputeSizeWithIntrinsicDimensions(
      gfxContext* aRenderingContext, mozilla::WritingMode aWM,
      const mozilla::IntrinsicSize& aIntrinsicSize,
      const mozilla::AspectRatio& aAspectRatio,
      const mozilla::LogicalSize& aCBSize, const mozilla::LogicalSize& aMargin,
      const mozilla::LogicalSize& aBorderPadding,
      const mozilla::StyleSizeOverrides& aSizeOverrides,
      mozilla::ComputeSizeFlags aFlags);

  // Compute tight bounds assuming this frame honours its border, background
  // and outline, its children's tight bounds, and nothing else.
  nsRect ComputeSimpleTightBounds(mozilla::gfx::DrawTarget* aDrawTarget) const;

  /*
   * If this frame is dirty, marks all absolutely-positioned children of this
   * frame dirty. If this frame isn't dirty, or if there are no
   * absolutely-positioned children, does nothing.
   *
   * It's necessary to use PushDirtyBitToAbsoluteFrames() when you plan to
   * reflow this frame's absolutely-positioned children after the dirty bit on
   * this frame has already been cleared, which prevents ReflowInput from
   * propagating the dirty bit normally. This situation generally only arises
   * when a multipass layout algorithm is used.
   */
  void PushDirtyBitToAbsoluteFrames();

  // Helper function that tests if the frame tree is too deep; if it is
  // it marks the frame as "unflowable", zeroes out the metrics, sets
  // the reflow status, and returns true. Otherwise, the frame is
  // unmarked "unflowable" and the metrics and reflow status are not
  // touched and false is returned.
  bool IsFrameTreeTooDeep(const ReflowInput& aReflowInput,
                          ReflowOutput& aMetrics, nsReflowStatus& aStatus);

  /**
   * @return true if we should avoid a page/column break in this frame.
   */
  bool ShouldAvoidBreakInside(const ReflowInput& aReflowInput) const;

  /**
   * To be called by |BuildDisplayLists| of this class or derived classes to add
   * a translucent overlay if this frame's content is selected.
   * @param aContentType an nsISelectionDisplay DISPLAY_ constant identifying
   * which kind of content this is for
   */
  void DisplaySelectionOverlay(
      nsDisplayListBuilder* aBuilder, nsDisplayList* aList,
      uint16_t aContentType = nsISelectionDisplay::DISPLAY_FRAMES);

  /**
   * Helper for ruby frames that want to accumulate the max ascent/descent of
   * their children.
   */
  mozilla::RubyMetrics RubyMetricsIncludingChildren(
      float aRubyMetricsFactor) const;

  // ==========================================================================

#ifdef DEBUG
  // A helper for flex / grid container to sanity check child lists before
  // reflow. Intended to be called after calling NormalizeChildLists().
  void SanityCheckChildListsBeforeReflow() const;

  // A helper to set mDidPushItemsBitMayLie if needed. Intended to be called
  // only in flex / grid container's RemoveFrame.
  void SetDidPushItemsBitIfNeeded(ChildListID aListID, nsIFrame* aOldFrame);

  // A flag for flex / grid containers. If true, NS_STATE_GRID_DID_PUSH_ITEMS or
  // NS_STATE_FLEX_DID_PUSH_ITEMS may be set even though all pushed frames may
  // have been removed. This is used to suppress an assertion in case
  // RemoveFrame removed all associated child frames.
  bool mDidPushItemsBitMayLie{false};
#endif

  nsFrameList mFrames;
};

// ==========================================================================
/* The out-of-flow-related code below is for a hacky way of splitting
 * absolutely-positioned frames. Basically what we do is split the frame
 * in nsAbsoluteContainingBlock and pretend the continuation is an overflow
 * container. This isn't an ideal solution, but it lets us print the content
 * at least. See bug 154892.
 */

/**
 * Helper class for tracking overflow container continuations during reflow.
 *
 * A frame is related to two sets of overflow containers: those that /are/
 * its own children, and those that are /continuations/ of its children.
 * This tracker walks through those continuations (the frame's NIF's children)
 * and their prev-in-flows (a subset of the frame's normal and overflow
 * container children) in parallel. It allows the reflower to synchronously
 * walk its overflow continuations while it loops through and reflows its
 * children. This makes it possible to insert new continuations at the correct
 * place in the overflow containers list.
 *
 * The reflower is expected to loop through its children in the same order it
 * looped through them the last time (if there was a last time).
 * For each child, the reflower should either
 *   - call Skip for the child if was not reflowed in this pass
 *   - call Insert for the overflow continuation if the child was reflowed
 *     but has incomplete overflow
 *   - call Finished for the child if it was reflowed in this pass but
 *     is either complete or has a normal next-in-flow. This call can
 *     be skipped if the child did not previously have an overflow
 *     continuation.
 */
class nsOverflowContinuationTracker {
 public:
  /**
   * Initializes an nsOverflowContinuationTracker to help track overflow
   * continuations of aFrame's children. Typically invoked on 'this'.
   *
   * aWalkOOFFrames determines whether the walker skips out-of-flow frames
   * or skips non-out-of-flow frames.
   *
   * Don't set aSkipOverflowContainerChildren to false unless you plan
   * to walk your own overflow container children. (Usually they are handled
   * by calling ReflowOverflowContainerChildren.) aWalkOOFFrames is ignored
   * if aSkipOverflowContainerChildren is false.
   */
  nsOverflowContinuationTracker(nsContainerFrame* aFrame, bool aWalkOOFFrames,
                                bool aSkipOverflowContainerChildren = true);
  /**
   * This function adds an overflow continuation to our running list and
   * sets its NS_FRAME_IS_OVERFLOW_CONTAINER flag.
   *
   * aReflowStatus should preferably be specific to the recently-reflowed
   * child and not influenced by any of its siblings' statuses. This
   * function sets the NS_FRAME_IS_DIRTY bit on aOverflowCont if it needs
   * to be reflowed. (Its need for reflow depends on changes to its
   * prev-in-flow, not to its parent--for whom it is invisible, reflow-wise.)
   *
   * The caller MUST disconnect the frame from its parent's child list
   * if it was not previously an NS_FRAME_IS_OVERFLOW_CONTAINER (because
   * StealFrame is much more inefficient than disconnecting in place
   * during Reflow, which the caller is able to do but we are not).
   *
   * The caller MUST NOT disconnect the frame from its parent's
   * child list if it is already an NS_FRAME_IS_OVERFLOW_CONTAINER.
   * (In this case we will disconnect and reconnect it ourselves.)
   */
  nsresult Insert(nsIFrame* aOverflowCont, nsReflowStatus& aReflowStatus);
  /**
   * Begin/EndFinish() must be called for each child that is reflowed
   * but no longer has an overflow continuation. (It may be called for
   * other children, but in that case has no effect.) It increments our
   * walker and makes sure we drop any dangling pointers to its
   * next-in-flow. This function MUST be called before stealing or
   * deleting aChild's next-in-flow.
   * The AutoFinish helper object does that for you. Use it like so:
   * if (kidNextInFlow) {
   *   nsOverflowContinuationTracker::AutoFinish fini(tracker, kid);
   *   ... DeleteNextInFlowChild/StealFrame(kidNextInFlow) here ...
   * }
   */
  class MOZ_RAII AutoFinish {
   public:
    AutoFinish(nsOverflowContinuationTracker* aTracker, nsIFrame* aChild)
        : mTracker(aTracker), mChild(aChild) {
      if (mTracker) {
        mTracker->BeginFinish(mChild);
      }
    }
    ~AutoFinish() {
      if (mTracker) {
        mTracker->EndFinish(mChild);
      }
    }

   private:
    nsOverflowContinuationTracker* mTracker;
    nsIFrame* mChild;
  };

  /**
   * This function should be called for each child that isn't reflowed.
   * It increments our walker and sets the mOverflowIncomplete
   * reflow flag if it encounters an overflow continuation so that our
   * next-in-flow doesn't get prematurely deleted. It MUST be called on
   * each unreflowed child that has an overflow container continuation;
   * it MAY be called on other children, but it isn't necessary (doesn't
   * do anything).
   */
  void Skip(nsIFrame* aChild, nsReflowStatus& aReflowStatus) {
    MOZ_ASSERT(aChild, "null ptr");
    if (aChild == mSentry) {
      StepForward();
      if (aReflowStatus.IsComplete()) {
        aReflowStatus.SetOverflowIncomplete();
      }
    }
  }

 private:
  /**
   * @see class AutoFinish
   */
  void BeginFinish(nsIFrame* aChild);
  void EndFinish(nsIFrame* aChild);

  void SetupOverflowContList();
  void SetUpListWalker();
  void StepForward();

  /* We hold a pointer to either the next-in-flow's overflow containers list
     or, if that doesn't exist, our frame's excess overflow containers list.
     We need to make sure that we drop that pointer if the list becomes
     empty and is deleted elsewhere. */
  nsFrameList* mOverflowContList;
  /* We hold a pointer to the most recently-reflowed child that has an
     overflow container next-in-flow. We do this because it's a known
     good point; this pointer won't be deleted on us. We can use it to
     recover our place in the list. */
  nsIFrame* mPrevOverflowCont;
  /* This is a pointer to the next overflow container's prev-in-flow, which
     is (or should be) a child of our frame. When we hit this, we will need
     to increment this walker to the next overflow container. */
  nsIFrame* mSentry;
  /* Parent of all frames in mOverflowContList. If our mOverflowContList
     is an excessOverflowContainersProperty, or null, then this is our frame
     (the frame that was passed in to our constructor). Otherwise this is
     that frame's next-in-flow, and our mOverflowContList is mParent's
     overflowContainersProperty */
  nsContainerFrame* mParent;
  /* Tells SetUpListWalker whether or not to walk us past any continuations
     of overflow containers. aWalkOOFFrames is ignored when this is false. */
  bool mSkipOverflowContainerChildren;
  /* Tells us whether to pay attention to OOF frames or non-OOF frames */
  bool mWalkOOFFrames;
};

#endif /* nsContainerFrame_h_ */
