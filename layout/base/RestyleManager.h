/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_RestyleManager_h
#define mozilla_RestyleManager_h

#include "mozilla/AutoRestore.h"
#include "mozilla/Maybe.h"
#include "mozilla/OverflowChangedTracker.h"
#include "mozilla/ServoElementSnapshot.h"
#include "mozilla/ServoElementSnapshotTable.h"
#include "nsChangeHint.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"  // XXX Shouldn't be included by header though
#include "nsStringFwd.h"
#include "nsTHashSet.h"

class nsAttrValue;
class nsAtom;
class nsIFrame;
class nsStyleChangeList;
class nsStyleChangeList;

namespace mozilla {

class ServoStyleSet;

namespace dom {
class Document;
class Element;
}  // namespace dom

/**
 * A stack class used to pass some common restyle state in a slightly more
 * comfortable way than a bunch of individual arguments, and that also checks
 * that the change hint used for optimization is correctly used in debug mode.
 */
class ServoRestyleState {
 public:
  ServoRestyleState(
      ServoStyleSet& aStyleSet, nsStyleChangeList& aChangeList,
      nsTArray<nsIFrame*>& aPendingWrapperRestyles,
      nsTArray<RefPtr<dom::Element>>& aPendingScrollAnchorSuppressions)
      : mStyleSet(aStyleSet),
        mChangeList(aChangeList),
        mPendingWrapperRestyles(aPendingWrapperRestyles),
        mPendingScrollAnchorSuppressions(aPendingScrollAnchorSuppressions),
        mPendingWrapperRestyleOffset(aPendingWrapperRestyles.Length()),
        mChangesHandled(nsChangeHint(0))
#ifdef DEBUG
        // If !mOwner, then we wouldn't have processed our wrapper restyles,
        // because we only process those when handling an element with a frame.
        // But that's OK, because if we started our traversal at an element with
        // no frame (e.g. it's display:contents), that means the wrapper frames
        // in our list actually inherit from one of its ancestors, not from it,
        // and hence not restyling them is OK.
        ,
        mAssertWrapperRestyleLength(false)
#endif  // DEBUG
  {
  }

  // We shouldn't assume that changes handled from our parent are handled for
  // our children too if we're out of flow since they aren't necessarily
  // parented in DOM order, and thus a change handled by a DOM ancestor doesn't
  // necessarily mean that it's handled for an ancestor frame.
  enum class CanUseHandledHints : bool { No = false, Yes };

  ServoRestyleState(const nsIFrame& aOwner, ServoRestyleState& aParentState,
                    nsChangeHint aHintForThisFrame,
                    CanUseHandledHints aCanUseHandledHints,
                    bool aAssertWrapperRestyleLength = true)
      : mStyleSet(aParentState.mStyleSet),
        mChangeList(aParentState.mChangeList),
        mPendingWrapperRestyles(aParentState.mPendingWrapperRestyles),
        mPendingScrollAnchorSuppressions(
            aParentState.mPendingScrollAnchorSuppressions),
        mPendingWrapperRestyleOffset(
            aParentState.mPendingWrapperRestyles.Length()),
        mChangesHandled(bool(aCanUseHandledHints)
                            ? aParentState.mChangesHandled | aHintForThisFrame
                            : aHintForThisFrame)
#ifdef DEBUG
        ,
        mOwner(&aOwner),
        mAssertWrapperRestyleLength(aAssertWrapperRestyleLength)
#endif
  {
    if (bool(aCanUseHandledHints)) {
      AssertOwner(aParentState);
    }
  }

  ~ServoRestyleState() {
    MOZ_ASSERT(
        !mAssertWrapperRestyleLength ||
            mPendingWrapperRestyles.Length() == mPendingWrapperRestyleOffset,
        "Someone forgot to call ProcessWrapperRestyles!");
  }

  nsStyleChangeList& ChangeList() { return mChangeList; }
  ServoStyleSet& StyleSet() { return mStyleSet; }

#ifdef DEBUG
  void AssertOwner(const ServoRestyleState& aParentState) const;
  nsChangeHint ChangesHandledFor(const nsIFrame*) const;
#else
  void AssertOwner(const ServoRestyleState&) const {}
  nsChangeHint ChangesHandledFor(const nsIFrame*) const {
    return mChangesHandled;
  }
#endif

  // Add a pending wrapper restyle.  We don't have to do anything if the thing
  // being added is already last in the list, but otherwise we do want to add
  // it, in order for ProcessWrapperRestyles to work correctly.
  void AddPendingWrapperRestyle(nsIFrame* aWrapperFrame);

  // Process wrapper restyles for this restyle state.  This should be done
  // before it comes off the stack.
  void ProcessWrapperRestyles(nsIFrame* aParentFrame);

  // Get the table-aware parent for the given child.  This will walk through
  // outer table and cellcontent frames.
  static nsIFrame* TableAwareParentFor(const nsIFrame* aChild);

  // When the value of the position property changes such as we stop or start
  // being absolutely or fixed positioned, we need to suppress scroll anchoring
  // adjustments to avoid breaking websites.
  //
  // We do need to process all this once we're done with all our reframes,
  // to handle correctly the cases where we reconstruct an ancestor, like when
  // you reframe an ib-split (see bug 1559627 for example).
  //
  // This doesn't handle nested reframes. We'd need to rework quite some code to
  // do that, and so far it doesn't seem to be a problem in practice.
  void AddPendingScrollAnchorSuppression(dom::Element* aElement) {
    mPendingScrollAnchorSuppressions.AppendElement(aElement);
  }

 private:
  // Process a wrapper restyle at the given index, and restyles for any
  // wrappers nested in it.  Returns the number of entries from
  // mPendingWrapperRestyles that we processed.  The return value is always at
  // least 1.
  size_t ProcessMaybeNestedWrapperRestyle(nsIFrame* aParent, size_t aIndex);

  ServoStyleSet& mStyleSet;
  nsStyleChangeList& mChangeList;

  // A list of pending wrapper restyles.  Anonymous box wrapper frames that need
  // restyling are added to this list when their non-anonymous kids are
  // restyled.  This avoids us having to do linear searches along the frame tree
  // for these anonymous boxes.  The problem then becomes that we can have
  // multiple kids all with the same anonymous parent, and we don't want to
  // restyle it more than once.  We use mPendingWrapperRestyles to track which
  // anonymous wrapper boxes we've requested be restyled and which of them have
  // already been restyled.  We use a single array propagated through
  // ServoRestyleStates by reference, because in a situation like this:
  //
  //  <div style="display: table"><span></span></div>
  //
  // We have multiple wrappers to restyle (cell, row, table-row-group) and we
  // want to add them in to the list all at once but restyle them using
  // different ServoRestyleStates with different owners.  When this situation
  // occurs, the relevant frames will be placed in the array with ancestors
  // before descendants.
  nsTArray<nsIFrame*>& mPendingWrapperRestyles;

  nsTArray<RefPtr<dom::Element>>& mPendingScrollAnchorSuppressions;

  // Since we're given a possibly-nonempty mPendingWrapperRestyles to start
  // with, we need to keep track of where the part of it we're responsible for
  // starts.
  size_t mPendingWrapperRestyleOffset;

  const nsChangeHint mChangesHandled;

  // We track the "owner" frame of this restyle state, that is, the frame that
  // generated the last change that is stored in mChangesHandled, in order to
  // verify that we only use mChangesHandled for actual descendants of that
  // frame (given DOM order isn't always frame order, and that there are a few
  // special cases for stuff like wrapper frames, ::backdrop, and so on).
#ifdef DEBUG
  const nsIFrame* mOwner{nullptr};
#endif

  // Whether we should assert in our destructor that we've processed all of the
  // relevant wrapper restyles.
#ifdef DEBUG
  const bool mAssertWrapperRestyleLength;
#endif  // DEBUG
};

enum class ServoPostTraversalFlags : uint32_t;

class RestyleManager {
  friend class dom::Document;
  friend class ServoStyleSet;

 public:
  typedef ServoElementSnapshotTable SnapshotTable;
  typedef mozilla::dom::Element Element;

  // Get an integer that increments every time we process pending restyles.
  // The value is never 0.
  uint64_t GetRestyleGeneration() const { return mRestyleGeneration; }
  // Unlike GetRestyleGeneration, which means the actual restyling count,
  // GetUndisplayedRestyleGeneration represents any possible DOM changes that
  // can cause restyling. This is needed for getComputedStyle to work with
  // non-styled (e.g. display: none) elements.
  uint64_t GetUndisplayedRestyleGeneration() const {
    return mUndisplayedRestyleGeneration;
  }

  void Disconnect() { mPresContext = nullptr; }

  ~RestyleManager() {
    MOZ_ASSERT(!mAnimationsWithDestroyedFrame,
               "leaving dangling pointers from AnimationsWithDestroyedFrame");
    MOZ_ASSERT(!mReentrantChanges);
  }

#ifdef DEBUG
  static nsCString ChangeHintToString(nsChangeHint aHint);

  /**
   * DEBUG ONLY method to verify integrity of style tree versus frame tree
   */
  void DebugVerifyStyleTree(nsIFrame* aFrame);
#endif

  void FlushOverflowChangedTracker() { mOverflowChangedTracker.Flush(); }

  // Should be called when a frame is going to be destroyed and
  // WillDestroyFrameTree hasn't been called yet.
  void NotifyDestroyingFrame(nsIFrame* aFrame) {
    mOverflowChangedTracker.RemoveFrame(aFrame);
    // If ProcessRestyledFrames is tracking frames which have been
    // destroyed (to avoid re-visiting them), add this one to its set.
    if (mDestroyedFrames) {
      mDestroyedFrames->Insert(aFrame);
    }
  }

  // Note: It's the caller's responsibility to make sure to wrap a
  // ProcessRestyledFrames call in a view update batch and a script blocker.
  // This function does not call ProcessAttachedQueue() on the binding manager.
  // If the caller wants that to happen synchronously, it needs to handle that
  // itself.
  void ProcessRestyledFrames(nsStyleChangeList& aChangeList);

  bool IsInStyleRefresh() const { return mInStyleRefresh; }

  // AnimationsWithDestroyedFrame is used to stop animations and transitions
  // on elements that have no frame at the end of the restyling process.
  // It only lives during the restyling process.
  class MOZ_STACK_CLASS AnimationsWithDestroyedFrame final {
   public:
    // Construct a AnimationsWithDestroyedFrame object.  The caller must
    // ensure that aRestyleManager lives at least as long as the
    // object.  (This is generally easy since the caller is typically a
    // method of RestyleManager.)
    explicit AnimationsWithDestroyedFrame(RestyleManager* aRestyleManager);

    // This method takes the content node for the generated content for
    // animation/transition on ::before and ::after, rather than the
    // content node for the real element.
    void Put(nsIContent* aContent, ComputedStyle* aComputedStyle) {
      MOZ_ASSERT(aContent);
      PseudoStyleType pseudoType = aComputedStyle->GetPseudoType();
      if (pseudoType == PseudoStyleType::NotPseudo ||
          PseudoStyle::IsViewTransitionPseudoElement(pseudoType)) {
        mContents.AppendElement(aContent->AsElement());
      } else if (pseudoType == PseudoStyleType::before) {
        MOZ_ASSERT(aContent->NodeInfo()->NameAtom() ==
                   nsGkAtoms::mozgeneratedcontentbefore);
        mBeforeContents.AppendElement(aContent->GetParent()->AsElement());
      } else if (pseudoType == PseudoStyleType::after) {
        MOZ_ASSERT(aContent->NodeInfo()->NameAtom() ==
                   nsGkAtoms::mozgeneratedcontentafter);
        mAfterContents.AppendElement(aContent->GetParent()->AsElement());
      } else if (pseudoType == PseudoStyleType::marker) {
        MOZ_ASSERT(aContent->NodeInfo()->NameAtom() ==
                   nsGkAtoms::mozgeneratedcontentmarker);
        mMarkerContents.AppendElement(aContent->GetParent()->AsElement());
      }
    }

    void StopAnimationsForElementsWithoutFrames();

   private:
    void StopAnimationsWithoutFrame(nsTArray<RefPtr<Element>>& aArray,
                                    const PseudoStyleRequest& aPseudoRequest);

    RestyleManager* mRestyleManager;
    AutoRestore<AnimationsWithDestroyedFrame*> mRestorePointer;

    // Below four arrays might include elements that have already had their
    // animations or transitions stopped.
    //
    // mBeforeContents, mAfterContents and mMarkerContents hold the real element
    // rather than the content node for the generated content (which might
    // change during a reframe).
    nsTArray<RefPtr<Element>> mContents;
    nsTArray<RefPtr<Element>> mBeforeContents;
    nsTArray<RefPtr<Element>> mAfterContents;
    nsTArray<RefPtr<Element>> mMarkerContents;
  };

  /**
   * Return the current AnimationsWithDestroyedFrame struct, or null if we're
   * not currently in a restyling operation.
   */
  AnimationsWithDestroyedFrame* GetAnimationsWithDestroyedFrame() {
    return mAnimationsWithDestroyedFrame;
  }

  void ContentInserted(nsIContent* aChild);
  void ContentAppended(nsIContent* aFirstNewContent);

  // Restyling for a content removal that is about to happen.
  void ContentWillBeRemoved(nsIContent* aOldChild);

  // Restyling for a ContentInserted (notification after insertion) or
  // for some CharacterDataChanged.
  void RestyleForInsertOrChange(nsIContent* aChild);

  // Restyle for a CharacterDataChanged notification. In practice this can only
  // affect :empty / :-moz-only-whitespace / :-moz-first-node / :-moz-last-node.
  void CharacterDataChanged(nsIContent*, const CharacterDataChangeInfo&);

  void PostRestyleEvent(dom::Element*, RestyleHint,
                        nsChangeHint aMinChangeHint);

  /**
   * Posts restyle hints for animations.
   * This is only called for the second traversal for CSS animations during
   * updating CSS animations in a SequentialTask.
   * This function does neither register a refresh observer nor flag that a
   * style flush is needed since this function is supposed to be called during
   * restyling process and this restyle event will be processed in the second
   * traversal of the same restyling process.
   */
  void PostRestyleEventForAnimations(dom::Element*, const PseudoStyleRequest&,
                                     RestyleHint);

  void NextRestyleIsForCSSRuleChanges() { mRestyleForCSSRuleChanges = true; }

  void RebuildAllStyleData(nsChangeHint aExtraHint, RestyleHint);

  void ProcessPendingRestyles();
  void ProcessAllPendingAttributeAndStateInvalidations();

  void ElementStateChanged(Element*, dom::ElementState);

  void CustomStatesWillChange(Element&);
  void CustomStateChanged(Element&, nsAtom* aState);
  void MaybeRestyleForNthOfCustomState(ServoStyleSet&, Element&,
                                       nsAtom* aState);

  /**
   * Posts restyle hints for siblings of an element and their descendants if the
   * element's parent has NODE_HAS_SLOW_SELECTOR_NTH_OF and the element has a
   * relevant state dependency.
   */
  void MaybeRestyleForNthOfState(ServoStyleSet& aStyleSet, dom::Element* aChild,
                                 dom::ElementState aChangedBits);

  void AttributeWillChange(Element* aElement, int32_t aNameSpaceID,
                           nsAtom* aAttribute, int32_t aModType);
  void ClassAttributeWillBeChangedBySMIL(dom::Element* aElement);
  void AttributeChanged(dom::Element* aElement, int32_t aNameSpaceID,
                        nsAtom* aAttribute, int32_t aModType,
                        const nsAttrValue* aOldValue);

  /**
   * Restyle an element's previous and/or next siblings.
   */
  void RestyleSiblingsForNthOf(dom::Element* aChild,
                               NodeSelectorFlags aParentFlags);

  /**
   * Posts restyle hints for siblings of an element and their descendants if the
   * element's parent has NODE_HAS_SLOW_SELECTOR_NTH_OF and the element has a
   * relevant attribute dependency.
   */
  void MaybeRestyleForNthOfAttribute(dom::Element* aChild, int32_t aNameSpaceID,
                                     nsAtom* aAttribute,
                                     const nsAttrValue* aOldValue);

  void MaybeRestyleForRelativeSelectorAttribute(dom::Element* aElement,
                                                int32_t aNameSpaceID,
                                                nsAtom* aAttribute,
                                                const nsAttrValue* aOldValue);
  void MaybeRestyleForRelativeSelectorState(ServoStyleSet& aStyleSet,
                                            dom::Element* aElement,
                                            dom::ElementState aChangedBits);

  // This is only used to reparent things when moving them in/out of the
  // ::first-line.
  void ReparentComputedStyleForFirstLine(nsIFrame*);

  /**
   * Performs a Servo animation-only traversal to compute style for all nodes
   * with the animation-only dirty bit in the document.
   *
   * This processes just the traversal for animation-only restyles and skips the
   * normal traversal for other restyles unrelated to animations.
   * This is used to bring throttled animations up-to-date such as when we need
   * to get correct position for transform animations that are throttled because
   * they are running on the compositor.
   *
   * This will traverse all of the document's style roots (that is, its document
   * element, and the roots of the document-level native anonymous content).
   */
  void UpdateOnlyAnimationStyles();

  // Get a counter that increments on every style change, that we use to
  // track whether off-main-thread animations are up-to-date.
  uint64_t GetAnimationGeneration() const { return mAnimationGeneration; }

  // Typically only style frames have animations associated with them so this
  // will likely return zero for anything that is not a style frame.
  static uint64_t GetAnimationGenerationForFrame(nsIFrame* aStyleFrame);

  // Update the animation generation count to mark that animation state
  // has changed.
  //
  // This is normally performed automatically by ProcessPendingRestyles
  // but it is also called when we have out-of-band changes to animations
  // such as changes made through the Web Animations API or cascading result
  // changes by modifying classes, etc.
  void IncrementAnimationGeneration() {
    ++mAnimationGeneration;
  }

  // Apply change hints for animations on the compositor.
  //
  // There are some cases where we forcibly apply change hints for animations
  // even if there is no change hint produced in order to synchronize with
  // animations running on the compositor.
  //
  // For example:
  //
  // a) Pausing animations via the Web Animations API
  // b) When the style before sending the animation to the compositor exactly
  // the same as the current style
  static void AddLayerChangesForAnimation(
      nsIFrame* aStyleFrame, nsIFrame* aPrimaryFrame, Element* aElement,
      nsChangeHint aHintForThisFrame, nsStyleChangeList& aChangeListToProcess);

  /**
   * Whether to clear all the style data (including the element itself), or just
   * the descendants' data.
   */
  enum class IncludeRoot {
    Yes,
    No,
  };

  /**
   * Clears the ServoElementData and HasDirtyDescendants from all elements
   * in the subtree rooted at aElement.
   */
  static void ClearServoDataFromSubtree(Element*,
                                        IncludeRoot = IncludeRoot::Yes);

  /**
   * Clears HasDirtyDescendants and RestyleData from all elements in the
   * subtree rooted at aElement.
   */
  static void ClearRestyleStateFromSubtree(Element* aElement);

  explicit RestyleManager(nsPresContext* aPresContext);

 protected:
  /**
   * Reparent the descendants of aFrame.  This is used by ReparentComputedStyle
   * and shouldn't be called by anyone else.  aProviderChild, if non-null, is a
   * child that was the style parent for aFrame and hence shouldn't be
   * reparented.
   */
  void ReparentFrameDescendants(nsIFrame* aFrame, nsIFrame* aProviderChild,
                                ServoStyleSet& aStyleSet);

  /**
   * Performs post-Servo-traversal processing on this element and its
   * descendants.
   *
   * Returns whether any style did actually change. There may be cases where we
   * didn't need to change any style after all, for example, when a content
   * attribute changes that happens not to have any effect on the style of that
   * element or any descendant or sibling.
   */
  bool ProcessPostTraversal(Element* aElement, ServoRestyleState& aRestyleState,
                            ServoPostTraversalFlags aFlags);

  struct TextPostTraversalState;
  bool ProcessPostTraversalForText(nsIContent* aTextNode,
                                   TextPostTraversalState& aState,
                                   ServoRestyleState& aRestyleState,
                                   ServoPostTraversalFlags aFlags);

  ServoStyleSet* StyleSet() const { return PresContext()->StyleSet(); }

  void RestyleWholeContainer(nsINode* aContainer, NodeSelectorFlags);
  void RestylePreviousSiblings(nsIContent* aStartingSibling);
  void RestyleSiblingsStartingWith(nsIContent* aStartingSibling);

  void RestyleForEmptyChange(Element* aContainer);
  void MaybeRestyleForEdgeChildChange(nsINode* aContainer,
                                      nsIContent* aChangedChild);

  bool IsDisconnected() const { return !mPresContext; }

  void IncrementRestyleGeneration() {
    if (++mRestyleGeneration == 0) {
      // Keep mRestyleGeneration from being 0, since that's what
      // nsPresContext::GetRestyleGeneration returns when it no
      // longer has a RestyleManager.
      ++mRestyleGeneration;
    }
    IncrementUndisplayedRestyleGeneration();
  }

  void IncrementUndisplayedRestyleGeneration() {
    if (++mUndisplayedRestyleGeneration == 0) {
      // Ensure mUndisplayedRestyleGeneration > 0, for the same reason as
      // IncrementRestyleGeneration.
      ++mUndisplayedRestyleGeneration;
    }
  }

  nsPresContext* PresContext() const {
    MOZ_ASSERT(mPresContext);
    return mPresContext;
  }

 private:
  nsPresContext* mPresContext;  // weak, can be null after Disconnect().
  uint64_t mRestyleGeneration;
  uint64_t mUndisplayedRestyleGeneration;

  // Used to keep track of frames that have been destroyed during
  // ProcessRestyledFrames, so we don't try to touch them again even if
  // they're referenced again later in the changelist.
  mozilla::UniquePtr<nsTHashSet<const nsIFrame*>> mDestroyedFrames;

  // Containers we've already fully restyled / invalidated.
  nsTHashSet<RefPtr<nsINode>> mRestyledAsWholeContainer;

 protected:
  // True if we're in the middle of a nsRefreshDriver refresh
  bool mInStyleRefresh;

  // The total number of animation flushes by this frame constructor.
  // Used to keep the layer and animation manager in sync.
  uint64_t mAnimationGeneration;

  OverflowChangedTracker mOverflowChangedTracker;

  AnimationsWithDestroyedFrame* mAnimationsWithDestroyedFrame = nullptr;

  const SnapshotTable& Snapshots() const { return mSnapshots; }
  void ClearSnapshots();
  ServoElementSnapshot& SnapshotFor(Element&);
  void TakeSnapshotForAttributeChange(Element&, int32_t aNameSpaceID,
                                      nsAtom* aAttribute);

  void DoProcessPendingRestyles(ServoTraversalFlags aFlags);

  // Function to do the actual (recursive) work of
  // ReparentComputedStyleForFirstLine, once we have asserted the invariants
  // that only hold on the initial call.
  void DoReparentComputedStyleForFirstLine(nsIFrame*, ServoStyleSet&);

  // We use a separate data structure from nsStyleChangeList because we need a
  // frame to create nsStyleChangeList entries, and the primary frame may not be
  // attached yet.
  struct ReentrantChange {
    nsCOMPtr<nsIContent> mContent;
    nsChangeHint mHint;
  };
  typedef AutoTArray<ReentrantChange, 10> ReentrantChangeList;

  // Only non-null while processing change hints. See the comment in
  // ProcessPendingRestyles.
  ReentrantChangeList* mReentrantChanges = nullptr;

  // We use this flag to track if the current restyle contains any non-animation
  // update, which triggers a normal restyle, and so there might be any new
  // transition created later. Therefore, if this flag is true, we need to
  // increase mAnimationGeneration before creating new transitions, so their
  // creation sequence will be correct.
  bool mHaveNonAnimationRestyles = false;

  // Set to true when posting restyle events triggered by CSS rule changes.
  // This flag is cleared once ProcessPendingRestyles has completed.
  // When we process a traversal all descendants elements of the document
  // triggered by CSS rule changes, we will need to update all elements with
  // CSS animations.  We propagate TraversalRestyleBehavior::ForCSSRuleChanges
  // to traversal function if this flag is set.
  bool mRestyleForCSSRuleChanges = false;

  // A hashtable with the elements that have changed state or attributes, in
  // order to calculate restyle hints during the traversal.
  SnapshotTable mSnapshots;
};

}  // namespace mozilla

#endif
