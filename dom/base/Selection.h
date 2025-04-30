/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_Selection_h__
#define mozilla_Selection_h__

#include "mozilla/AutoRestore.h"
#include "mozilla/EventForwards.h"
#include "mozilla/PresShellForwards.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/SelectionChangeEventDispatcher.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/Highlight.h"
#include "mozilla/dom/StyledRange.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsDirection.h"
#include "nsISelectionController.h"
#include "nsISelectionListener.h"
#include "nsRange.h"
#include "nsTArrayForwardDeclare.h"
#include "nsThreadUtils.h"
#include "nsWeakReference.h"
#include "nsWrapperCache.h"

struct CachedOffsetForFrame;
class AutoScroller;
class nsIFrame;
class nsFrameSelection;
class nsPIDOMWindowOuter;
struct SelectionDetails;
struct SelectionCustomColors;
class nsCopySupport;
class nsHTMLCopyEncoder;
class nsPresContext;
struct nsPoint;
struct nsRect;

namespace mozilla {
class AccessibleCaretEventHub;
class ErrorResult;
class HTMLEditor;
class PostContentIterator;
enum class CaretAssociationHint;
enum class TableSelectionMode : uint32_t;
struct AutoPrepareFocusRange;
struct PrimaryFrameData;
namespace dom {
class DocGroup;
}  // namespace dom
}  // namespace mozilla

namespace mozilla {

enum class SelectionScrollMode : uint8_t {
  // Don't scroll synchronously. We'll flush when the scroll event fires so we
  // make sure to scroll to the right place.
  Async,
  // Scroll synchronously, without flushing layout.
  SyncNoFlush,
  // Scroll synchronously, flushing layout. You MUST hold a strong ref on
  // 'this' for the duration of this call.  This might destroy arbitrary
  // layout objects.
  SyncFlush,
};

namespace dom {

/**
 * This cache allows to store all selected nodes during a reflow operation.
 *
 * All fully selected nodes are stored in a hash set per-selection instance.
 * This allows fast paths in `nsINode::IsSelected()` and
 * `Selection::LookupSelection()`. For partially selected nodes, the old
 * mechanisms are used. This is okay, because for partially selected nodes
 * no expensive node traversal is necessary.
 *
 * This cache is designed to be used in a context where no script is allowed
 * to run. It assumes that the selection itself, or any range therein, does not
 * change during its lifetime.
 *
 * By design, this class can only be instantiated in the `PresShell`.
 */
class MOZ_RAII SelectionNodeCache final {
 public:
  ~SelectionNodeCache();
  /**
   * Returns true if `aNode` is fully selected by any of the given selections.
   *
   * This method will collect all fully selected nodes of `aSelections` and
   * store them internally (therefore this method isn't const).
   */
  bool MaybeCollectNodesAndCheckIfFullySelectedInAnyOf(
      const nsINode* aNode, const nsTArray<Selection*>& aSelections);

  /**
   * Returns true if `aNode` is fully selected by any range in `aSelection`.
   *
   * This method collects all fully selected nodes from `aSelection` and store
   * them internally.
   */
  bool MaybeCollectNodesAndCheckIfFullySelected(const nsINode* aNode,
                                                const Selection* aSelection) {
    return MaybeCollect(aSelection).Contains(aNode);
  }

 private:
  /**
   * This class is supposed to be only created by the PresShell.
   */
  friend PresShell;
  explicit SelectionNodeCache(PresShell& aOwningPresShell);
  /**
   * Iterates all ranges in `aSelection` and collects its fully selected nodes
   * into a hash set, which is also returned.
   *
   * If `aSelection` is already cached, the hash set is returned directly.
   */
  const nsTHashSet<const nsINode*>& MaybeCollect(const Selection* aSelection);

  nsTHashMap<const Selection*, nsTHashSet<const nsINode*>> mSelectedNodes;

  PresShell& mOwningPresShell;
};

// Note, the ownership of mozilla::dom::Selection depends on which way the
// object is created. When nsFrameSelection has created Selection,
// addreffing/releasing the Selection object is aggregated to nsFrameSelection.
// Otherwise normal addref/release is used.  This ensures that nsFrameSelection
// is never deleted before its Selections.
class Selection final : public nsSupportsWeakReference,
                        public nsWrapperCache,
                        public SupportsWeakPtr {
  using AllowRangeCrossShadowBoundary =
      mozilla::dom::AllowRangeCrossShadowBoundary;
  using IsUnlinking = AbstractRange::IsUnlinking;

 protected:
  virtual ~Selection();

 public:
  /**
   * @param aFrameSelection can be nullptr.
   */
  explicit Selection(SelectionType aSelectionType,
                     nsFrameSelection* aFrameSelection);

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(Selection)

  /**
   * Match this up with EndbatchChanges. will stop ui updates while multiple
   * selection methods are called
   *
   * @param aDetails string to explian why this is called.  This won't be
   * stored nor exposed to selection listeners etc.  Just for logging.
   */
  void StartBatchChanges(const char* aDetails);

  /**
   * Match this up with StartBatchChanges
   *
   * @param aDetails string to explian why this is called.  This won't be
   * stored nor exposed to selection listeners etc.  Just for logging.
   * @param aReasons potentially multiple of the reasons defined in
   * nsISelectionListener.idl
   */
  MOZ_CAN_RUN_SCRIPT void EndBatchChanges(
      const char* aDetails, int16_t aReason = nsISelectionListener::NO_REASON);

  /**
   * NotifyAutoCopy() starts to notify AutoCopyListener of selection changes.
   */
  void NotifyAutoCopy() {
    MOZ_ASSERT(mSelectionType == SelectionType::eNormal);

    mNotifyAutoCopy = true;
  }

  /**
   * MaybeNotifyAccessibleCaretEventHub() starts to notify
   * AccessibleCaretEventHub of selection change if aPresShell has it.
   */
  void MaybeNotifyAccessibleCaretEventHub(PresShell* aPresShell);

  /**
   * StopNotifyingAccessibleCaretEventHub() stops notifying
   * AccessibleCaretEventHub of selection change.
   */
  void StopNotifyingAccessibleCaretEventHub();

  /**
   * EnableSelectionChangeEvent() starts to notify
   * SelectionChangeEventDispatcher of selection change to dispatch a
   * selectionchange event at every selection change.
   */
  void EnableSelectionChangeEvent() {
    if (!mSelectionChangeEventDispatcher) {
      mSelectionChangeEventDispatcher = new SelectionChangeEventDispatcher();
    }
  }

  // Required for WebIDL bindings, see
  // https://developer.mozilla.org/en-US/docs/Mozilla/WebIDL_bindings#Adding_WebIDL_bindings_to_a_class.
  Document* GetParentObject() const;

  DocGroup* GetDocGroup() const;

  // utility methods for scrolling the selection into view
  nsPresContext* GetPresContext() const;
  PresShell* GetPresShell() const;
  nsFrameSelection* GetFrameSelection() const { return mFrameSelection; }
  // Returns a rect containing the selection region, and frame that that
  // position is relative to. For SELECTION_ANCHOR_REGION or
  // SELECTION_FOCUS_REGION the rect is a zero-width rectangle. For
  // SELECTION_WHOLE_SELECTION the rect contains both the anchor and focus
  // region rects.
  nsIFrame* GetSelectionAnchorGeometry(SelectionRegion aRegion, nsRect* aRect);
  // Returns the position of the region (SELECTION_ANCHOR_REGION or
  // SELECTION_FOCUS_REGION only), and frame that that position is relative to.
  // The 'position' is a zero-width rectangle.
  nsIFrame* GetSelectionEndPointGeometry(SelectionRegion aRegion,
                                         nsRect* aRect);

  nsresult PostScrollSelectionIntoViewEvent(SelectionRegion aRegion,
                                            ScrollFlags aFlags,
                                            ScrollAxis aVertical,
                                            ScrollAxis aHorizontal);

  MOZ_CAN_RUN_SCRIPT nsresult ScrollIntoView(
      SelectionRegion, ScrollAxis aVertical = ScrollAxis(),
      ScrollAxis aHorizontal = ScrollAxis(), ScrollFlags = ScrollFlags::None,
      SelectionScrollMode = SelectionScrollMode::Async);

 private:
  static bool IsUserSelectionCollapsed(
      const nsRange& aRange, nsTArray<RefPtr<nsRange>>& aTempRangesToAdd);
  /**
   * https://w3c.github.io/selection-api/#selectstart-event.
   */
  enum class DispatchSelectstartEvent {
    No,
    Maybe,
  };

  /**
   * See `AddRangesForSelectableNodes`.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult AddRangesForUserSelectableNodes(
      nsRange* aRange, Maybe<size_t>* aOutIndex,
      const DispatchSelectstartEvent aDispatchSelectstartEvent);

  /**
   * Adds aRange to this Selection.  If mUserInitiated is true,
   * then aRange is first scanned for -moz-user-select:none nodes and split up
   * into multiple ranges to exclude those before adding the resulting ranges
   * to this Selection.
   *
   * @param aOutIndex points to the range last added, if at least one was added.
   *                  If aRange is already contained, it points to the range
   *                  containing it. Nothing() if mStyledRanges.mRanges was
   *                  empty and no range was added.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT nsresult AddRangesForSelectableNodes(
      nsRange* aRange, Maybe<size_t>* aOutIndex,
      DispatchSelectstartEvent aDispatchSelectstartEvent);

  already_AddRefed<StaticRange> GetComposedRange(
      const AbstractRange* aRange,
      const Sequence<OwningNonNull<ShadowRoot>>& aShadowRoots) const;

 public:
  nsresult RemoveCollapsedRanges();
  void Clear(nsPresContext* aPresContext,
             IsUnlinking aIsUnlinking = IsUnlinking::No);
  MOZ_CAN_RUN_SCRIPT nsresult CollapseInLimiter(nsINode* aContainer,
                                                uint32_t aOffset) {
    if (!aContainer) {
      return NS_ERROR_INVALID_ARG;
    }
    return CollapseInLimiter(RawRangeBoundary(aContainer, aOffset));
  }
  MOZ_CAN_RUN_SCRIPT nsresult
  CollapseInLimiter(const RawRangeBoundary& aPoint) {
    ErrorResult result;
    CollapseInLimiter(aPoint, result);
    return result.StealNSResult();
  }
  MOZ_CAN_RUN_SCRIPT void CollapseInLimiter(const RawRangeBoundary& aPoint,
                                            ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT nsresult Extend(nsINode* aContainer, uint32_t aOffset);

  /**
   * See mStyledRanges.mRanges.
   */
  nsRange* GetRangeAt(uint32_t aIndex) const;

  /**
   * @brief Get the |AbstractRange| at |aIndex|.
   *
   * This method is safe to be called for every selection type.
   * However, |StaticRange|s only occur for |SelectionType::eHighlight|.
   * If the SelectionType may be eHighlight, this method must be called instead
   * of |GetRangeAt()|.
   *
   * Returns null if |aIndex| is out of bounds.
   */
  AbstractRange* GetAbstractRangeAt(uint32_t aIndex) const;
  // Get the anchor-to-focus range if we don't care which end is
  // anchor and which end is focus.
  const nsRange* GetAnchorFocusRange() const { return mAnchorFocusRange; }

  void GetDirection(nsAString& aDirection) const;

  nsDirection GetDirection() const { return mDirection; }

  void SetDirection(nsDirection aDir) { mDirection = aDir; }
  MOZ_CAN_RUN_SCRIPT nsresult SetAnchorFocusToRange(nsRange* aRange);

  MOZ_CAN_RUN_SCRIPT void ReplaceAnchorFocusRange(nsRange* aRange);

  void AdjustAnchorFocusForMultiRange(nsDirection aDirection);

  nsIFrame* GetPrimaryFrameForAnchorNode() const;

  /**
   * Get primary frame and some other data for putting caret or extending
   * selection at the focus point.
   */
  PrimaryFrameData GetPrimaryFrameForCaretAtFocusNode(bool aVisual) const;

  UniquePtr<SelectionDetails> LookUpSelection(
      nsIContent* aContent, uint32_t aContentOffset, uint32_t aContentLength,
      UniquePtr<SelectionDetails> aDetailsHead, SelectionType aSelectionType,
      bool aSlowCheck);

  NS_IMETHOD Repaint(nsPresContext* aPresContext);

  MOZ_CAN_RUN_SCRIPT
  nsresult StartAutoScrollTimer(nsIFrame* aFrame, const nsPoint& aPoint,
                                uint32_t aDelayInMs);

  nsresult StopAutoScrollTimer();

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  // WebIDL methods
  nsINode* GetAnchorNode(CallerType aCallerType = CallerType::System) const {
    const RangeBoundary& anchor = AnchorRef();
    nsINode* anchorNode = anchor.IsSet() ? anchor.GetContainer() : nullptr;
    if (!anchorNode || aCallerType == CallerType::System ||
        !anchorNode->ChromeOnlyAccess()) {
      return anchorNode;
    }
    // anchor is nsIContent as ChromeOnlyAccess is nsIContent-only
    return anchorNode->AsContent()->FindFirstNonChromeOnlyAccessContent();
  }
  uint32_t AnchorOffset(CallerType aCallerType = CallerType::System) const {
    const RangeBoundary& anchor = AnchorRef();
    if (aCallerType != CallerType::System && anchor.IsSet() &&
        anchor.GetContainer()->ChromeOnlyAccess()) {
      return 0;
    }
    const Maybe<uint32_t> offset =
        anchor.Offset(RangeBoundary::OffsetFilter::kValidOffsets);
    return offset ? *offset : 0;
  }
  nsINode* GetFocusNode(CallerType aCallerType = CallerType::System) const {
    const RangeBoundary& focus = FocusRef();
    nsINode* focusNode = focus.IsSet() ? focus.GetContainer() : nullptr;
    if (!focusNode || aCallerType == CallerType::System ||
        !focusNode->ChromeOnlyAccess()) {
      return focusNode;
    }
    // focus is nsIContent as ChromeOnlyAccess is nsIContent-only
    return focusNode->AsContent()->FindFirstNonChromeOnlyAccessContent();
  }
  uint32_t FocusOffset(CallerType aCallerType = CallerType::System) const {
    const RangeBoundary& focus = FocusRef();
    if (aCallerType != CallerType::System && focus.IsSet() &&
        focus.GetContainer()->ChromeOnlyAccess()) {
      return 0;
    }
    const Maybe<uint32_t> offset =
        focus.Offset(RangeBoundary::OffsetFilter::kValidOffsets);
    return offset ? *offset : 0;
  }

  nsINode* GetMayCrossShadowBoundaryAnchorNode() const {
    const RangeBoundary& anchor = AnchorRef(AllowRangeCrossShadowBoundary::Yes);
    return anchor.IsSet() ? anchor.GetContainer() : nullptr;
  }

  uint32_t MayCrossShadowBoundaryAnchorOffset() const {
    const RangeBoundary& anchor = AnchorRef(AllowRangeCrossShadowBoundary::Yes);
    const Maybe<uint32_t> offset =
        anchor.Offset(RangeBoundary::OffsetFilter::kValidOffsets);
    return offset ? *offset : 0;
  }

  nsINode* GetMayCrossShadowBoundaryFocusNode() const {
    const RangeBoundary& focus = FocusRef(AllowRangeCrossShadowBoundary::Yes);
    return focus.IsSet() ? focus.GetContainer() : nullptr;
  }

  uint32_t MayCrossShadowBoundaryFocusOffset() const {
    const RangeBoundary& focus = FocusRef(AllowRangeCrossShadowBoundary::Yes);
    const Maybe<uint32_t> offset =
        focus.Offset(RangeBoundary::OffsetFilter::kValidOffsets);
    return offset ? *offset : 0;
  }

  nsIContent* GetChildAtAnchorOffset() {
    const RangeBoundary& anchor = AnchorRef();
    return anchor.IsSet() ? anchor.GetChildAtOffset() : nullptr;
  }
  nsIContent* GetChildAtFocusOffset() {
    const RangeBoundary& focus = FocusRef();
    return focus.IsSet() ? focus.GetChildAtOffset() : nullptr;
  }

  const RangeBoundary& AnchorRef(
      AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary =
          AllowRangeCrossShadowBoundary::No) const;
  const RangeBoundary& FocusRef(
      AllowRangeCrossShadowBoundary aAllowCrossShadowBoundary =
          AllowRangeCrossShadowBoundary::No) const;

  /*
   * IsCollapsed -- is the whole selection just one point, or unset?
   */
  bool IsCollapsed() const {
    size_t cnt = mStyledRanges.Length();
    if (cnt == 0) {
      return true;
    }

    if (cnt != 1) {
      return false;
    }

    return mStyledRanges.mRanges[0].mRange->Collapsed();
  }

  // Returns whether both normal range and cross-shadow-boundary
  // range are collapsed.
  //
  // If StaticPrefs::dom_shadowdom_selection_across_boundary_enabled is
  // disabled, this method always returns result as nsRange::IsCollapsed.
  bool AreNormalAndCrossShadowBoundaryRangesCollapsed() const {
    if (!IsCollapsed()) {
      return false;
    }

    size_t cnt = mStyledRanges.Length();
    if (cnt == 0) {
      return true;
    }

    AbstractRange* range = mStyledRanges.mRanges[0].mRange;
    MOZ_ASSERT_IF(
        range->MayCrossShadowBoundary(),
        !range->AsDynamicRange()->CrossShadowBoundaryRangeCollapsed());
    // Returns false if nsRange::mCrossBoundaryRange exists,
    // true otherwise.
    return !range->MayCrossShadowBoundary();
  }

  // *JS() methods are mapped to Selection.*().
  // They may move focus only when the range represents normal selection.
  // These methods shouldn't be used by non-JS callers.
  MOZ_CAN_RUN_SCRIPT void CollapseJS(nsINode* aContainer, uint32_t aOffset,
                                     mozilla::ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void CollapseToStartJS(mozilla::ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void CollapseToEndJS(mozilla::ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void ExtendJS(nsINode& aContainer, uint32_t aOffset,
                                   mozilla::ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void SelectAllChildrenJS(nsINode& aNode,
                                              mozilla::ErrorResult& aRv);

  /**
   * Deletes this selection from document the nodes belong to.
   * Only if this has `SelectionType::eNormal`.
   */
  MOZ_CAN_RUN_SCRIPT void DeleteFromDocument(mozilla::ErrorResult& aRv);

  uint32_t RangeCount() const { return mStyledRanges.Length(); }

  void GetType(nsAString& aOutType) const;

  nsRange* GetRangeAt(uint32_t aIndex, mozilla::ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT void AddRangeJS(nsRange& aRange,
                                     mozilla::ErrorResult& aRv);

  /**
   * Callers need to keep `aRange` alive.
   */
  MOZ_CAN_RUN_SCRIPT void RemoveRangeAndUnselectFramesAndNotifyListeners(
      AbstractRange& aRange, mozilla::ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void RemoveAllRanges(mozilla::ErrorResult& aRv);

  void GetComposedRanges(
      const Sequence<OwningNonNull<ShadowRoot>>& aShadowRoots,
      nsTArray<RefPtr<StaticRange>>& aComposedRanges);

  /**
   * Whether Stringify should flush layout or not.
   */
  enum class FlushFrames { No, Yes };
  MOZ_CAN_RUN_SCRIPT
  void Stringify(nsAString& aResult,
                 CallerType aCallerType = CallerType::System,
                 FlushFrames = FlushFrames::Yes);

  /**
   * Indicates whether the node is part of the selection. If partlyContained
   * is true, the function returns true when some part of the node
   * is part of the selection. If partlyContained is false, the
   * function only returns true when the entire node is part of the selection.
   */
  bool ContainsNode(nsINode& aNode, bool aPartlyContained,
                    mozilla::ErrorResult& aRv);

  /**
   * Check to see if the given point is contained within the selection area. In
   * particular, this iterates through all the rects that make up the selection,
   * not just the bounding box, and checks to see if the given point is
   * contained in any one of them.
   * @param aPoint The point to check, relative to the root frame.
   */
  bool ContainsPoint(const nsPoint& aPoint);

  /**
   * Modifies the selection.  Note that the parameters are case-insensitive.
   *
   * @param alter can be one of { "move", "extend" }
   *   - "move" collapses the selection to the end of the selection and
   *      applies the movement direction/granularity to the collapsed
   *      selection.
   *   - "extend" leaves the start of the selection unchanged, and applies
   *      movement direction/granularity to the end of the selection.
   * @param direction can be one of { "forward", "backward", "left", "right" }
   * @param granularity can be one of { "character", "word",
   *                                    "line", "lineboundary" }
   */
  MOZ_CAN_RUN_SCRIPT void Modify(const nsAString& aAlter,
                                 const nsAString& aDirection,
                                 const nsAString& aGranularity);

  MOZ_CAN_RUN_SCRIPT
  void SetBaseAndExtentJS(nsINode& aAnchorNode, uint32_t aAnchorOffset,
                          nsINode& aFocusNode, uint32_t aFocusOffset,
                          mozilla::ErrorResult& aRv);

  bool GetInterlinePositionJS(mozilla::ErrorResult& aRv) const;
  void SetInterlinePositionJS(bool aHintRight, mozilla::ErrorResult& aRv);

  enum class InterlinePosition : uint8_t {
    // Caret should be put at end of line (i.e., before the line break)
    EndOfLine,
    // Caret should be put at start of next line (i.e., after the line break)
    StartOfNextLine,
    // Undefined means only what is not EndOfLine nor StartOfNextLine.
    // `SetInterlinePosition` should never be called with this value, and
    // if `GetInterlinePosition` returns this, it means that the instance has
    // not been initialized or cleared by the cycle collector or something.
    // If a method needs to consider whether to call `SetInterlinePosition` or
    // not call, this value can be used for the latter.
    Undefined,
  };
  InterlinePosition GetInterlinePosition() const;
  nsresult SetInterlinePosition(InterlinePosition aInterlinePosition);

  Nullable<int16_t> GetCaretBidiLevel(mozilla::ErrorResult& aRv) const;
  void SetCaretBidiLevel(const Nullable<int16_t>& aCaretBidiLevel,
                         mozilla::ErrorResult& aRv);

  void ToStringWithFormat(const nsAString& aFormatType, uint32_t aFlags,
                          int32_t aWrapColumn, nsAString& aReturn,
                          mozilla::ErrorResult& aRv);
  void AddSelectionListener(nsISelectionListener* aListener);
  void RemoveSelectionListener(nsISelectionListener* aListener);

  RawSelectionType RawType() const {
    return ToRawSelectionType(mSelectionType);
  }
  SelectionType Type() const { return mSelectionType; }

  /**
   * @brief Sets highlight selection properties.
   *
   * This includes the highlight name as well as its priority and type.
   */
  void SetHighlightSelectionData(
      dom::HighlightSelectionData aHighlightSelectionData);

  const dom::HighlightSelectionData& HighlightSelectionData() const {
    return mHighlightData;
  }

  /**
   * See documentation of `GetRangesForInterval` in Selection.webidl.
   *
   * @param aReturn references, not copies, of the internal ranges.
   */
  void GetRangesForInterval(nsINode& aBeginNode, uint32_t aBeginOffset,
                            nsINode& aEndNode, uint32_t aEndOffset,
                            bool aAllowAdjacent,
                            nsTArray<RefPtr<nsRange>>& aReturn,
                            ErrorResult& aRv);

  void SetColors(const nsAString& aForeColor, const nsAString& aBackColor,
                 const nsAString& aAltForeColor, const nsAString& aAltBackColor,
                 ErrorResult& aRv);

  void ResetColors();

  /**
   * Non-JS callers should use the following
   * collapse/collapseToStart/extend/etc methods, instead of the *JS
   * versions that bindings call.
   */

  /**
   * Collapses the selection to a single point, at the specified offset
   * in the given node. When the selection is collapsed, and the content
   * is focused and editable, the caret will blink there.
   * @param aContainer The given node where the selection will be set
   * @param aOffset     Where in given dom node to place the selection (the
   *                    offset into the given node)
   */
  MOZ_CAN_RUN_SCRIPT void CollapseInLimiter(nsINode& aContainer,
                                            uint32_t aOffset,
                                            ErrorResult& aRv) {
    CollapseInLimiter(RawRangeBoundary(&aContainer, aOffset), aRv);
  }

 private:
  enum class InLimiter {
    // If eYes, the method may reset selection limiter and move focus if the
    // given range is out of the limiter.
    eYes,
    // If eNo, the method won't reset selection limiter.  So, if given range
    // is out of bounds, the method may return error.
    eNo,
  };
  MOZ_CAN_RUN_SCRIPT
  void CollapseInternal(InLimiter aInLimiter, const RawRangeBoundary& aPoint,
                        ErrorResult& aRv);

 public:
  /**
   * Collapses the whole selection to a single point at the start
   * of the current selection (irrespective of direction).  If content
   * is focused and editable, the caret will blink there.
   */
  MOZ_CAN_RUN_SCRIPT void CollapseToStart(mozilla::ErrorResult& aRv);

  /**
   * Collapses the whole selection to a single point at the end
   * of the current selection (irrespective of direction).  If content
   * is focused and editable, the caret will blink there.
   */
  MOZ_CAN_RUN_SCRIPT void CollapseToEnd(mozilla::ErrorResult& aRv);

  /**
   * Extends the selection by moving the selection end to the specified node and
   * offset, preserving the selection begin position. The new selection end
   * result will always be from the anchorNode to the new focusNode, regardless
   * of direction.
   *
   * @param aContainer The node where the selection will be extended to
   * @param aOffset    Where in aContainer to place the offset of the new
   *                   selection end.
   */
  MOZ_CAN_RUN_SCRIPT void Extend(nsINode& aContainer, uint32_t aOffset,
                                 ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void AddRangeAndSelectFramesAndNotifyListeners(
      nsRange& aRange, mozilla::ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void AddHighlightRangeAndSelectFramesAndNotifyListeners(
      AbstractRange& aRange);

  /**
   * Adds all children of the specified node to the selection.
   * @param aNode the parent of the children to be added to the selection.
   */
  MOZ_CAN_RUN_SCRIPT void SelectAllChildren(nsINode& aNode,
                                            mozilla::ErrorResult& aRv);

  /**
   * SetStartAndEnd() removes all ranges and sets new range as given range.
   * Different from SetBaseAndExtent(), this won't compare the DOM points of
   * aStartRef and aEndRef for performance nor set direction to eDirPrevious.
   * Note that this may reset the limiter and move focus.  If you don't want
   * that, use SetStartAndEndInLimiter() instead.
   */
  MOZ_CAN_RUN_SCRIPT
  void SetStartAndEnd(const RawRangeBoundary& aStartRef,
                      const RawRangeBoundary& aEndRef, ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT
  void SetStartAndEnd(nsINode& aStartContainer, uint32_t aStartOffset,
                      nsINode& aEndContainer, uint32_t aEndOffset,
                      ErrorResult& aRv) {
    SetStartAndEnd(RawRangeBoundary(&aStartContainer, aStartOffset),
                   RawRangeBoundary(&aEndContainer, aEndOffset), aRv);
  }

  /**
   * SetStartAndEndInLimiter() is similar to SetStartAndEnd(), but this respects
   * the selection limiter.  If all or part of given range is not in the
   * limiter, this returns error.
   */
  MOZ_CAN_RUN_SCRIPT
  void SetStartAndEndInLimiter(const RawRangeBoundary& aStartRef,
                               const RawRangeBoundary& aEndRef,
                               ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT
  void SetStartAndEndInLimiter(nsINode& aStartContainer, uint32_t aStartOffset,
                               nsINode& aEndContainer, uint32_t aEndOffset,
                               ErrorResult& aRv) {
    SetStartAndEndInLimiter(RawRangeBoundary(&aStartContainer, aStartOffset),
                            RawRangeBoundary(&aEndContainer, aEndOffset), aRv);
  }
  MOZ_CAN_RUN_SCRIPT
  Result<Ok, nsresult> SetStartAndEndInLimiter(
      nsINode& aStartContainer, uint32_t aStartOffset, nsINode& aEndContainer,
      uint32_t aEndOffset, nsDirection aDirection, int16_t aReason);

  /**
   * SetBaseAndExtent() is alternative of the JS API for internal use.
   * Different from SetStartAndEnd(), this sets anchor and focus points as
   * specified, then if anchor point is after focus node, this sets the
   * direction to eDirPrevious.
   * Note that this may reset the limiter and move focus.  If you don't want
   * that, use SetBaseAndExtentInLimier() instead.
   */
  MOZ_CAN_RUN_SCRIPT
  void SetBaseAndExtent(nsINode& aAnchorNode, uint32_t aAnchorOffset,
                        nsINode& aFocusNode, uint32_t aFocusOffset,
                        ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT
  void SetBaseAndExtent(const RawRangeBoundary& aAnchorRef,
                        const RawRangeBoundary& aFocusRef, ErrorResult& aRv);

  /**
   * SetBaseAndExtentInLimiter() is similar to SetBaseAndExtent(), but this
   * respects the selection limiter.  If all or part of given range is not in
   * the limiter, this returns error.
   */
  MOZ_CAN_RUN_SCRIPT
  void SetBaseAndExtentInLimiter(nsINode& aAnchorNode, uint32_t aAnchorOffset,
                                 nsINode& aFocusNode, uint32_t aFocusOffset,
                                 ErrorResult& aRv) {
    SetBaseAndExtentInLimiter(RawRangeBoundary(&aAnchorNode, aAnchorOffset),
                              RawRangeBoundary(&aFocusNode, aFocusOffset), aRv);
  }
  MOZ_CAN_RUN_SCRIPT
  void SetBaseAndExtentInLimiter(const RawRangeBoundary& aAnchorRef,
                                 const RawRangeBoundary& aFocusRef,
                                 ErrorResult& aRv);

  void AddSelectionChangeBlocker();
  void RemoveSelectionChangeBlocker();
  bool IsBlockingSelectionChangeEvents() const;

  // Whether this selection is focused in an editable element.
  bool IsEditorSelection() const;

  /**
   * Set the painting style for the range. The range must be a range in
   * the selection. The textRangeStyle will be used by text frame
   * when it is painting the selection.
   */
  nsresult SetTextRangeStyle(nsRange* aRange,
                             const TextRangeStyle& aTextRangeStyle);

  // Methods to manipulate our mFrameSelection's ancestor limiter.
  [[nodiscard]] Element* GetAncestorLimiter() const;
  MOZ_CAN_RUN_SCRIPT void SetAncestorLimiter(Element* aLimiter);

  /*
   * Frame Offset cache can be used just during calling
   * nsEditor::EndPlaceHolderTransaction. EndPlaceHolderTransaction will give
   * rise to reflow/refreshing view/scroll, and call times of
   * nsTextFrame::GetPointFromOffset whose return value is to be cached. see
   * bugs 35296 and 199412
   */
  void SetCanCacheFrameOffset(bool aCanCacheFrameOffset);

  // Selection::GetAbstractRangesForIntervalArray
  //
  //    Fills a nsTArray with the ranges overlapping the range specified by
  //    the given endpoints. Ranges in the selection exactly adjacent to the
  //    input range are not returned unless aAllowAdjacent is set.
  //
  //    For example, if the following ranges were in the selection
  //    (assume everything is within the same node)
  //
  //    Start Offset: 0 2 7 9
  //      End Offset: 2 5 9 10
  //
  //    and passed aBeginOffset of 2 and aEndOffset of 9, then with
  //    aAllowAdjacent set, all the ranges should be returned. If
  //    aAllowAdjacent was false, the ranges [2, 5] and [7, 9] only
  //    should be returned
  //
  //    Now that overlapping ranges are disallowed, there can be a maximum of
  //    2 adjacent ranges
  nsresult GetAbstractRangesForIntervalArray(nsINode* aBeginNode,
                                             uint32_t aBeginOffset,
                                             nsINode* aEndNode,
                                             uint32_t aEndOffset,
                                             bool aAllowAdjacent,
                                             nsTArray<AbstractRange*>* aRanges);

  /**
   * Converts the results of |GetAbstractRangesForIntervalArray()| to |nsRange|.
   *
   * |StaticRange|s can only occur in Selections of type |eHighlight|.
   * Therefore, this method must not be called for this selection type
   * as not every |AbstractRange| can be cast to |nsRange|.
   */
  nsresult GetDynamicRangesForIntervalArray(
      nsINode* aBeginNode, uint32_t aBeginOffset, nsINode* aEndNode,
      uint32_t aEndOffset, bool aAllowAdjacent, nsTArray<nsRange*>* aRanges);

  /**
   * Modifies the cursor Bidi level after a change in keyboard direction
   * @param langRTL is true if the new language is right-to-left or
   *                false if the new language is left-to-right.
   */
  nsresult SelectionLanguageChange(bool aLangRTL);

 private:
  bool HasSameRootOrSameComposedDoc(const nsINode& aNode);

  // XXX Please don't add additional uses of this method, it's only for
  // XXX supporting broken code (bug 1245883) in the following classes:
  friend class ::nsCopySupport;
  friend class ::nsHTMLCopyEncoder;
  MOZ_CAN_RUN_SCRIPT
  void AddRangeAndSelectFramesAndNotifyListenersInternal(nsRange& aRange,
                                                         Document* aDocument,
                                                         ErrorResult&);

  // Get the cached value for nsTextFrame::GetPointFromOffset.
  nsresult GetCachedFrameOffset(nsIFrame* aFrame, int32_t inOffset,
                                nsPoint& aPoint);

  MOZ_CAN_RUN_SCRIPT
  void SetStartAndEndInternal(InLimiter aInLimiter,
                              const RawRangeBoundary& aStartRef,
                              const RawRangeBoundary& aEndRef,
                              nsDirection aDirection, ErrorResult& aRv);
  MOZ_CAN_RUN_SCRIPT
  void SetBaseAndExtentInternal(InLimiter aInLimiter,
                                const RawRangeBoundary& aAnchorRef,
                                const RawRangeBoundary& aFocusRef,
                                ErrorResult& aRv);

 public:
  SelectionType GetType() const { return mSelectionType; }

  SelectionCustomColors* GetCustomColors() const { return mCustomColors.get(); }

  MOZ_CAN_RUN_SCRIPT void NotifySelectionListeners(bool aCalledByJS);
  MOZ_CAN_RUN_SCRIPT void NotifySelectionListeners();

  bool ChangesDuringBatching() const { return mChangesDuringBatching; }

  friend struct AutoUserInitiated;
  struct MOZ_RAII AutoUserInitiated {
    explicit AutoUserInitiated(Selection& aSelectionRef)
        : AutoUserInitiated(&aSelectionRef) {}
    explicit AutoUserInitiated(Selection* aSelection)
        : mSavedValue(aSelection->mUserInitiated) {
      aSelection->mUserInitiated = true;
    }
    AutoRestore<bool> mSavedValue;
  };

 private:
  friend struct mozilla::AutoPrepareFocusRange;
  class ScrollSelectionIntoViewEvent;
  friend class ScrollSelectionIntoViewEvent;

  class ScrollSelectionIntoViewEvent : public Runnable {
   public:
    MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_DECL_NSIRUNNABLE

    ScrollSelectionIntoViewEvent(Selection* aSelection, SelectionRegion aRegion,
                                 ScrollAxis aVertical, ScrollAxis aHorizontal,
                                 ScrollFlags aFlags)
        : Runnable("dom::Selection::ScrollSelectionIntoViewEvent"),
          mSelection(aSelection),
          mRegion(aRegion),
          mVerticalScroll(aVertical),
          mHorizontalScroll(aHorizontal),
          mFlags(aFlags) {
      NS_ASSERTION(aSelection, "null parameter");
    }
    void Revoke() { mSelection = nullptr; }

   private:
    Selection* mSelection;
    SelectionRegion mRegion;
    ScrollAxis mVerticalScroll;
    ScrollAxis mHorizontalScroll;
    ScrollFlags mFlags;
  };

  /**
   * Set mAnchorFocusRange to mStyledRanges.mRanges[aIndex] if aIndex is a valid
   * index.
   */
  void SetAnchorFocusRange(size_t aIndex);
  void RemoveAnchorFocusRange() { mAnchorFocusRange = nullptr; }
  void SelectFramesOf(nsIContent* aContent, bool aSelected) const;

  /**
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-descendant.
   */
  nsresult SelectFramesOfInclusiveDescendantsOfContent(
      PostContentIterator& aPostOrderIter, nsIContent* aContent,
      bool aSelected) const;

  /**
   * https://dom.spec.whatwg.org/#concept-shadow-including-descendant
   */
  void SelectFramesOfShadowIncludingDescendantsOfContent(nsIContent* aContent,
                                                         bool aSelected) const;

  nsresult SelectFrames(nsPresContext* aPresContext, AbstractRange& aRange,
                        bool aSelect) const;

  /**
   * SelectFramesInAllRanges() calls SelectFrames() for all current
   * ranges.
   */
  void SelectFramesInAllRanges(nsPresContext* aPresContext);

  /**
   * @param aOutIndex   If some, points to the index of the range in
   * mStyledRanges.mRanges so that it's always in [0, mStyledRanges.Length()].
   * Otherwise, if nothing, this didn't add the range to mStyledRanges.
   */
  MOZ_CAN_RUN_SCRIPT nsresult MaybeAddTableCellRange(nsRange& aRange,
                                                     Maybe<size_t>* aOutIndex);

  Document* GetDocument() const;

  MOZ_CAN_RUN_SCRIPT void RemoveAllRangesInternal(
      mozilla::ErrorResult& aRv, IsUnlinking aIsUnlinking = IsUnlinking::No);

  void Disconnect();

  struct StyledRanges {
    explicit StyledRanges(Selection& aSelection) : mSelection(aSelection) {}
    void Clear();

    StyledRange* FindRangeData(AbstractRange* aRange);

    using StyledRangeArray = AutoTArray<StyledRange, 1>;

    StyledRangeArray::size_type Length() const;

    nsresult RemoveCollapsedRanges();

    nsresult RemoveRangeAndUnregisterSelection(AbstractRange& aRange);

    /**
     * Binary searches the given sorted array of ranges for the insertion point
     * for the given aBoundary. The given comparator is used, and the index
     * where the point should appear in the array is returned.

     * If there is an item in the array equal to aBoundary, we will return the
     index of this item.
     *
     * @return the index where the point should appear in the array. In
     *         [0, `aElementArray->Length()`].
     */
    template <typename PT, typename RT>
    static size_t FindInsertionPoint(
        const nsTArray<StyledRange>* aElementArray,
        const RangeBoundaryBase<PT, RT>& aBoundary,
        int32_t (*aComparator)(const RangeBoundaryBase<PT, RT>&,
                               const AbstractRange&));

    /**
     * Works on the same principle as GetRangesForIntervalArray, however
     * instead this returns the indices into mRanges between which
     * the overlapping ranges lie.
     *
     * @param aStartIndex If some, aEndIndex will also be some and the value of
     *                    aStartIndex will be less or equal than aEndIndex.  If
     *                    nothing, aEndIndex will also be nothing and it means
     *                    that there is no range which in the range.
     * @param aEndIndex   If some, the value is less than mRanges.Length().
     */
    nsresult GetIndicesForInterval(const nsINode* aBeginNode,
                                   uint32_t aBeginOffset,
                                   const nsINode* aEndNode, uint32_t aEndOffset,
                                   bool aAllowAdjacent,
                                   Maybe<size_t>& aStartIndex,
                                   Maybe<size_t>& aEndIndex);

    bool HasEqualRangeBoundariesAt(const AbstractRange& aRange,
                                   size_t aRangeIndex) const;

    /**
     * Preserves the sorting and disjunctiveness of mRanges.
     *
     * @param aOutIndex If some, will point to the index of the added range, or
     *                  if aRange is already contained, to the one containing
     *                  it. Hence it'll always be in [0, mRanges.Length()).
     *                  This is nothing only when the method returns an error.
     */
    MOZ_CAN_RUN_SCRIPT nsresult
    MaybeAddRangeAndTruncateOverlaps(nsRange* aRange, Maybe<size_t>* aOutIndex);

    /**
     * Adds the range even if there are overlaps.
     */
    MOZ_CAN_RUN_SCRIPT nsresult
    AddRangeAndIgnoreOverlaps(AbstractRange* aRange);

    /**
     * GetCommonEditingHost() returns common editing host of all
     * ranges if there is. If at least one of the ranges is in non-editable
     * element, returns nullptr.  See following examples for the detail:
     *
     *  <div id="a" contenteditable>
     *    an[cestor
     *    <div id="b" contenteditable="false">
     *      non-editable
     *      <div id="c" contenteditable>
     *        desc]endant
     *  in this case, this returns div#a because div#c is also in div#a.
     *
     *  <div id="a" contenteditable>
     *    an[ce]stor
     *    <div id="b" contenteditable="false">
     *      non-editable
     *      <div id="c" contenteditable>
     *        de[sc]endant
     *  in this case, this returns div#a because second range is also in div#a
     *  and common ancestor of the range (i.e., div#c) is editable.
     *
     *  <div id="a" contenteditable>
     *    an[ce]stor
     *    <div id="b" contenteditable="false">
     *      [non]-editable
     *      <div id="c" contenteditable>
     *        de[sc]endant
     *  in this case, this returns nullptr because the second range is in
     *  non-editable area.
     */
    Element* GetCommonEditingHost() const;

    MOZ_CAN_RUN_SCRIPT void MaybeFocusCommonEditingHost(
        PresShell* aPresShell) const;

    static nsresult SubtractRange(StyledRange& aRange, nsRange& aSubtract,
                                  nsTArray<StyledRange>* aOutput);

    void UnregisterSelection(IsUnlinking aIsUnlinking = IsUnlinking::No);

    // `mRanges` always needs to be sorted by the Range's start point.
    // Especially when dealing with `StaticRange`s this is not guaranteed
    // automatically. Therefore this method should be called before paint to
    // ensure that any potential DOM mutations are incorporated in `mRanges`
    // order. This method will also move invalid `StaticRange`s into
    // `mInvalidStaticRanges` (and previously-invalid-now-valid-again
    // `StaticRange`s back into `mRanges`).
    void ReorderRangesIfNecessary();

    // These are the ranges inside this selection. They are kept sorted in order
    // of DOM start position.
    //
    // This data structure is sorted by the range beginnings. As the ranges are
    // disjoint, it is also implicitly sorted by the range endings. This allows
    // us to perform binary searches when searching for existence of a range,
    // giving us O(log n) search time.
    //
    // Inserting a new range requires finding the overlapping interval,
    // requiring two binary searches plus up to an additional 6 DOM comparisons.
    // If this proves to be a performance concern, then an interval tree may be
    // a possible solution, allowing the calculation of the overlap interval in
    // O(log n) time, though this would require rebalancing and other overhead.
    StyledRangeArray mRanges;

    // With introduction of the custom highlight API, Selection must be able to
    // hold `StaticRange`s as well. If they become invalid (eg. end is before
    // start), they must be excluded from painting, but still kept.
    // mRanges needs to contain valid ranges sorted correctly only. Therefore,
    // invalid static ranges are being stored in this array, which is being kept
    // up to date in `ReorderRangesIfNecessary()`.
    StyledRangeArray mInvalidStaticRanges;

    Selection& mSelection;

    // The Document's generation for which `mRanges` have been ordered.
    int32_t mDocumentGeneration{0};
    // This flag indicates that ranges may have changed. It is set to true in
    // `Selection::NotifySelectionListeners().`
    bool mRangesMightHaveChanged{false};
  };

  StyledRanges mStyledRanges{*this};

  RefPtr<nsRange> mAnchorFocusRange;
  RefPtr<nsFrameSelection> mFrameSelection;
  RefPtr<AccessibleCaretEventHub> mAccessibleCaretEventHub;
  RefPtr<SelectionChangeEventDispatcher> mSelectionChangeEventDispatcher;
  RefPtr<AutoScroller> mAutoScroller;
  nsTArray<nsCOMPtr<nsISelectionListener>> mSelectionListeners;
  nsRevocableEventPtr<ScrollSelectionIntoViewEvent> mScrollEvent;
  CachedOffsetForFrame* mCachedOffsetForFrame;
  nsDirection mDirection;
  const SelectionType mSelectionType;
  dom::HighlightSelectionData mHighlightData;
  UniquePtr<SelectionCustomColors> mCustomColors;

  // Non-zero if we don't want any changes we make to the selection to be
  // visible to content. If non-zero, content won't be notified about changes.
  uint32_t mSelectionChangeBlockerCount;

  /**
   * True if the current selection operation was initiated by user action.
   * It determines whether we exclude -moz-user-select:none nodes or not,
   * as well as whether selectstart events will be fired.
   */
  bool mUserInitiated;

  /**
   * When the selection change is caused by a call of Selection API,
   * mCalledByJS is true.  Otherwise, false.
   */
  bool mCalledByJS;

  /**
   * true if AutoCopyListner::OnSelectionChange() should be called.
   */
  bool mNotifyAutoCopy;

  /**
   * Indicates that this selection has changed during a batch change and
   * `NotifySelectionListener()` should be called after batching ends.
   *
   * See `nsFrameSelection::StartBatchChanges()` and `::EndBatchChanges()`.
   *
   * This flag is set and reset in `NotifySelectionListener()`.
   */
  bool mChangesDuringBatching = false;
};

// Stack-class to turn on/off selection batching.
class MOZ_STACK_CLASS SelectionBatcher final {
 private:
  const RefPtr<Selection> mSelection;
  const int16_t mReasons;
  const char* const mRequesterFuncName;

 public:
  /**
   * @param aRequesterFuncName function name which wants the selection batch.
   * This won't be stored nor exposed to selection listeners etc, used only for
   * logging.  This MUST be living when the destructor runs.
   */
  MOZ_CAN_RUN_SCRIPT explicit SelectionBatcher(
      Selection& aSelectionRef, const char* aRequesterFuncName,
      int16_t aReasons = nsISelectionListener::NO_REASON)
      : SelectionBatcher(&aSelectionRef, aRequesterFuncName, aReasons) {}
  MOZ_CAN_RUN_SCRIPT explicit SelectionBatcher(
      Selection* aSelection, const char* aRequesterFuncName,
      int16_t aReasons = nsISelectionListener::NO_REASON)
      : mSelection(aSelection),
        mReasons(aReasons),
        mRequesterFuncName(aRequesterFuncName) {
    if (mSelection) {
      mSelection->StartBatchChanges(mRequesterFuncName);
    }
  }

  MOZ_CAN_RUN_SCRIPT ~SelectionBatcher() {
    if (mSelection) {
      MOZ_KnownLive(mSelection)->EndBatchChanges(mRequesterFuncName, mReasons);
    }
  }
};

class MOZ_RAII AutoHideSelectionChanges final {
 public:
  explicit AutoHideSelectionChanges(const nsFrameSelection* aFrame);

  explicit AutoHideSelectionChanges(Selection& aSelectionRef)
      : AutoHideSelectionChanges(&aSelectionRef) {}

  ~AutoHideSelectionChanges() {
    if (mSelection) {
      mSelection->RemoveSelectionChangeBlocker();
    }
  }

 private:
  explicit AutoHideSelectionChanges(Selection* aSelection)
      : mSelection(aSelection) {
    if (mSelection) {
      mSelection->AddSelectionChangeBlocker();
    }
  }

  RefPtr<Selection> mSelection;
};

}  // namespace dom

constexpr bool IsValidRawSelectionType(RawSelectionType aRawSelectionType) {
  return aRawSelectionType >= nsISelectionController::SELECTION_NONE &&
         aRawSelectionType <= nsISelectionController::SELECTION_TARGET_TEXT;
}

constexpr SelectionType ToSelectionType(RawSelectionType aRawSelectionType) {
  if (!IsValidRawSelectionType(aRawSelectionType)) {
    return SelectionType::eInvalid;
  }
  return static_cast<SelectionType>(aRawSelectionType);
}

constexpr RawSelectionType ToRawSelectionType(SelectionType aSelectionType) {
  MOZ_ASSERT(aSelectionType != SelectionType::eInvalid);
  return static_cast<RawSelectionType>(aSelectionType);
}

constexpr RawSelectionType ToRawSelectionType(TextRangeType aTextRangeType) {
  return ToRawSelectionType(ToSelectionType(aTextRangeType));
}

constexpr SelectionTypeMask ToSelectionTypeMask(SelectionType aSelectionType) {
  MOZ_ASSERT(aSelectionType != SelectionType::eInvalid);
  return aSelectionType == SelectionType::eNone
             ? 0
             : static_cast<SelectionTypeMask>(
                   1 << (static_cast<uint8_t>(aSelectionType) - 1));
}

inline std::ostream& operator<<(
    std::ostream& aStream, const dom::Selection::InterlinePosition& aPosition) {
  using InterlinePosition = dom::Selection::InterlinePosition;
  switch (aPosition) {
    case InterlinePosition::EndOfLine:
      return aStream << "InterlinePosition::EndOfLine";
    case InterlinePosition::StartOfNextLine:
      return aStream << "InterlinePosition::StartOfNextLine";
    case InterlinePosition::Undefined:
      return aStream << "InterlinePosition::Undefined";
    default:
      MOZ_ASSERT_UNREACHABLE("Illegal value");
      return aStream << "<Illegal value>";
  }
}

}  // namespace mozilla

inline nsresult nsISelectionController::ScrollSelectionIntoView(
    mozilla::SelectionType aType, SelectionRegion aRegion,
    const mozilla::ScrollAxis& aVertical = mozilla::ScrollAxis(),
    const mozilla::ScrollAxis& aHorizontal = mozilla::ScrollAxis(),
    mozilla::ScrollFlags aScrollFlags = mozilla::ScrollFlags::None,
    mozilla::SelectionScrollMode aMode = mozilla::SelectionScrollMode::Async) {
  RefPtr selection = GetSelection(mozilla::RawSelectionType(aType));
  if (!selection) {
    return NS_ERROR_FAILURE;
  }
  return selection->ScrollIntoView(aRegion, aVertical, aHorizontal,
                                   aScrollFlags, aMode);
}

inline nsresult nsISelectionController::ScrollSelectionIntoView(
    mozilla::SelectionType aType, SelectionRegion aRegion,
    mozilla::SelectionScrollMode aMode) {
  return ScrollSelectionIntoView(aType, aRegion, mozilla::ScrollAxis(),
                                 mozilla::ScrollAxis(),
                                 mozilla::ScrollFlags::None, aMode);
}

#endif  // mozilla_Selection_h__
