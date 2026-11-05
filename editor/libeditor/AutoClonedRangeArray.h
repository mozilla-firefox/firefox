/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AutoClonedRangeArray_h
#define AutoClonedRangeArray_h

#include "EditAction.h"      // for EditSubAction
#include "EditorBase.h"      // for EditorBase
#include "EditorDOMPoint.h"  // for EditorDOMPoint, EditorDOMRange, etc
#include "EditorForwards.h"
#include "HTMLEditHelpers.h"  // for BlockInlineCheck
#include "HTMLEditor.h"       // for HTMLEditor
#include "SelectionState.h"   // for SelectionState

#include "mozilla/ErrorResult.h"              // for ErrorResult
#include "mozilla/IntegerRange.h"             // for IntegerRange
#include "mozilla/Maybe.h"                    // for Maybe
#include "mozilla/RangeBoundary.h"            // for RangeBoundary
#include "mozilla/Result.h"                   // for Result<>
#include "mozilla/dom/Element.h"              // for dom::Element
#include "mozilla/dom/HTMLBRElement.h"        // for dom::HTMLBRElement
#include "mozilla/dom/Range.h"                // for dom::Range
#include "mozilla/dom/Selection.h"            // for dom::Selection
#include "mozilla/dom/Text.h"                 // for dom::Text
#include "mozilla/intl/BidiEmbeddingLevel.h"  // for BidiEmbeddingLevel

#include "nsDebug.h"           // for NS_WARNING, etc
#include "nsDirection.h"       // for nsDirection
#include "nsError.h"           // for NS_SUCCESS_* and NS_ERROR_*
#include "nsFrameSelection.h"  // for nsFrameSelection

namespace mozilla {

enum class CaretAssociationHint;

/******************************************************************************
 * AutoClonedRangeArray stores closed ranges and has similar API to Selection.
 *****************************************************************************/

class MOZ_STACK_CLASS AutoClonedRangeArray {
 public:
  template <typename PointType>
  explicit AutoClonedRangeArray(const EditorDOMRangeBase<PointType>& aRange);
  template <typename PT, typename CT>
  explicit AutoClonedRangeArray(const EditorDOMPointBase<PT, CT>& aPoint);
  explicit AutoClonedRangeArray(const dom::Range& aRange);
  // The copy constructor copies everything except saved ranges.
  explicit AutoClonedRangeArray(const AutoClonedRangeArray& aOther);

  virtual ~AutoClonedRangeArray() = default;

  /**
   * Check whether all ranges in content nodes or not.  If the ranges is empty,
   * this returns false.
   */
  [[nodiscard]] bool IsInContent() const {
    if (mRanges.IsEmpty()) {
      return false;
    }
    for (const OwningNonNull<dom::Range>& range : mRanges) {
      if (MOZ_UNLIKELY(!range->IsPositioned() || !range->GetStartContainer() ||
                       !range->GetStartContainer()->IsContent() ||
                       !range->GetEndContainer() ||
                       !range->GetEndContainer()->IsContent())) {
        return false;
      }
    }
    return true;
  }

  /**
   * EnsureOnlyEditableRanges() removes ranges which cannot modify.
   * Note that this is designed only for `HTMLEditor` because this must not
   * be required by `TextEditor`.
   */
  void EnsureOnlyEditableRanges(const dom::Element& aEditingHost);

  enum class RangeInReplacedOrVoidElement : bool {
    // Each range in a replaced or a void element should be collapsed before the
    // element.
    Collapse,
    // Each range in a replaced or a void element should be deleted.
    Delete,
  };

  /**
   * Adjust ranges if each boundary is in a replaced element or a void element.
   * If the adjusted range is not at proper position to edit, this will remove
   * the range.
   *
   * @return true if some ranges are modified.
   */
  bool AdjustRangesNotInReplacedNorVoidElements(
      RangeInReplacedOrVoidElement aRangeInReplacedOrVoidElement,
      const dom::Element& aEditingHost);

  /**
   * EnsureRangesInTextNode() is designed for TextEditor to guarantee that
   * all ranges are in its text node which is first child of the anonymous <div>
   * element and is first child.
   */
  void EnsureRangesInTextNode(const dom::Text& aTextNode);

  /**
   * Extend ranges to make each range select starting from a line start edge and
   * ending after a line end edge to handle per line edit sub-actions.
   */
  void ExtendRangesToWrapLines(EditSubAction aEditSubAction,
                               BlockInlineCheck aBlockInlineCheck,
                               const dom::Element& aAncestorLimiter);

  /**
   * Check whether the range is in aEditingHost and both containers of start and
   * end boundaries of the range are editable.
   */
  [[nodiscard]] static bool IsEditableRange(const dom::AbstractRange& aRange,
                                            const dom::Element& aEditingHost);

  /**
   * Check whether the first range is in aEditingHost and both containers of
   * start and end boundaries of the first range are editable.
   */
  [[nodiscard]] bool IsFirstRangeEditable(
      const dom::Element& aEditingHost) const {
    return IsEditableRange(FirstRangeRef(), aEditingHost);
  }

  /**
   * IsAtLeastOneContainerOfRangeBoundariesInclusiveDescendantOf() returns true
   * if at least one of the containers of the range boundaries is an inclusive
   * descendant of aContent.
   */
  [[nodiscard]] bool
  IsAtLeastOneContainerOfRangeBoundariesInclusiveDescendantOf(
      const nsIContent& aContent) const {
    for (const OwningNonNull<dom::Range>& range : mRanges) {
      nsINode* startContainer = range->GetStartContainer();
      if (startContainer &&
          startContainer->IsInclusiveDescendantOf(&aContent)) {
        return true;
      }
      nsINode* endContainer = range->GetEndContainer();
      if (startContainer == endContainer) {
        continue;
      }
      if (endContainer && endContainer->IsInclusiveDescendantOf(&aContent)) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] auto& Ranges() { return mRanges; }
  [[nodiscard]] const auto& Ranges() const { return mRanges; }
  [[nodiscard]] OwningNonNull<dom::Range>& FirstRangeRef() {
    return mRanges[0];
  }
  [[nodiscard]] const OwningNonNull<dom::Range>& FirstRangeRef() const {
    return mRanges[0];
  }

  template <template <typename> typename StrongPtrType>
  [[nodiscard]] AutoTArray<StrongPtrType<dom::Range>, 8> CloneRanges() const {
    AutoTArray<StrongPtrType<dom::Range>, 8> ranges;
    for (const auto& range : mRanges) {
      ranges.AppendElement(range->CloneRange());
    }
    return ranges;
  }

  template <typename EditorDOMPointType>
  [[nodiscard]] EditorDOMPointType GetFirstRangeStartPoint() const {
    if (mRanges.IsEmpty() || !mRanges[0]->IsPositioned()) {
      return EditorDOMPointType();
    }
    return EditorDOMPointType(mRanges[0]->StartRef());
  }
  template <typename EditorDOMPointType>
  [[nodiscard]] EditorDOMPointType GetFirstRangeEndPoint() const {
    if (mRanges.IsEmpty() || !mRanges[0]->IsPositioned()) {
      return EditorDOMPointType();
    }
    return EditorDOMPointType(mRanges[0]->EndRef());
  }

  nsresult SelectNode(nsINode& aNode) {
    mRanges.Clear();
    if (!mAnchorFocusRange) {
      mAnchorFocusRange = dom::Range::Create(&aNode);
      if (!mAnchorFocusRange) {
        return NS_ERROR_FAILURE;
      }
    }
    ErrorResult error;
    mAnchorFocusRange->SelectNode(aNode, error);
    if (error.Failed()) {
      mAnchorFocusRange = nullptr;
      return error.StealNSResult();
    }
    mRanges.AppendElement(*mAnchorFocusRange);
    return NS_OK;
  }

  /**
   * For compatiblity with the other browsers, we should shrink ranges to
   * start from an atomic content and/or end after one instead of start
   * from end of a preceding text node and end by start of a following text
   * node.  Returns true if this modifies a range.
   */
  enum class IfSelectingOnlyOneAtomicContent {
    Collapse,  // Collapse to the range selecting only one atomic content to
               // start or after of it.  Whether to collapse start or after
               // it depends on aDirectionAndAmount.  This is ignored if
               // there are multiple ranges.
    KeepSelecting,  // Won't collapse the range.
  };
  Result<bool, nsresult> ShrinkRangesIfStartFromOrEndAfterAtomicContent(
      const HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
      IfSelectingOnlyOneAtomicContent aIfSelectingOnlyOneAtomicContent);

  /**
   * The following methods are same as `Selection`'s methods.
   */
  [[nodiscard]] bool IsCollapsed() const {
    return mRanges.IsEmpty() ||
           (mRanges.Length() == 1 && mRanges[0]->Collapsed());
  }
  template <typename PT, typename CT>
  nsresult Collapse(const EditorDOMPointBase<PT, CT>& aPoint) {
    mRanges.Clear();
    if (!mAnchorFocusRange) {
      ErrorResult error;
      mAnchorFocusRange = dom::Range::Create(
          aPoint.ToRawRangeBoundary(), aPoint.ToRawRangeBoundary(), error);
      if (error.Failed()) {
        mAnchorFocusRange = nullptr;
        return error.StealNSResult();
      }
    } else {
      nsresult rv = mAnchorFocusRange->CollapseTo(aPoint.ToRawRangeBoundary());
      if (NS_FAILED(rv)) {
        mAnchorFocusRange = nullptr;
        return rv;
      }
    }
    mRanges.AppendElement(*mAnchorFocusRange);
    SetNewCaretAssociationHint(aPoint.ToRawRangeBoundary(),
                               aPoint.GetInterlinePosition());
    return NS_OK;
  }
  template <typename SPT, typename SCT, typename EPT, typename ECT>
  nsresult SetStartAndEnd(const EditorDOMPointBase<SPT, SCT>& aStart,
                          const EditorDOMPointBase<EPT, ECT>& aEnd) {
    mRanges.Clear();
    if (!mAnchorFocusRange) {
      ErrorResult error;
      mAnchorFocusRange = dom::Range::Create(aStart.ToRawRangeBoundary(),
                                             aEnd.ToRawRangeBoundary(), error);
      if (error.Failed()) {
        mAnchorFocusRange = nullptr;
        return error.StealNSResult();
      }
    } else {
      nsresult rv = mAnchorFocusRange->SetStartAndEnd(
          aStart.ToRawRangeBoundary(), aEnd.ToRawRangeBoundary());
      if (NS_FAILED(rv)) {
        mAnchorFocusRange = nullptr;
        return rv;
      }
    }
    mRanges.AppendElement(*mAnchorFocusRange);
    return NS_OK;
  }
  template <typename SPT, typename SCT, typename EPT, typename ECT>
  nsresult SetBaseAndExtent(const EditorDOMPointBase<SPT, SCT>& aAnchor,
                            const EditorDOMPointBase<EPT, ECT>& aFocus) {
    if (MOZ_UNLIKELY(!aAnchor.IsSet()) || MOZ_UNLIKELY(!aFocus.IsSet())) {
      mRanges.Clear();
      mAnchorFocusRange = nullptr;
      return NS_ERROR_INVALID_ARG;
    }
    return aAnchor.EqualsOrIsBefore(aFocus) ? SetStartAndEnd(aAnchor, aFocus)
                                            : SetStartAndEnd(aFocus, aAnchor);
  }
  [[nodiscard]] const dom::Range* GetAnchorFocusRange() const {
    return mAnchorFocusRange;
  }
  [[nodiscard]] nsDirection GetDirection() const { return mDirection; }

  void SetDirection(nsDirection aDirection) { mDirection = aDirection; }

  [[nodiscard]] const RangeBoundary& AnchorRef() const {
    if (!mAnchorFocusRange) {
      static RangeBoundary sEmptyRangeBoundary;
      return sEmptyRangeBoundary;
    }
    return mDirection == nsDirection::eDirNext ? mAnchorFocusRange->StartRef()
                                               : mAnchorFocusRange->EndRef();
  }
  [[nodiscard]] nsINode* GetAnchorNode() const {
    return AnchorRef().IsSet() ? AnchorRef().GetContainer() : nullptr;
  }
  [[nodiscard]] uint32_t GetAnchorOffset() const {
    return AnchorRef().IsSet()
               ? AnchorRef()
                     .Offset(RangeBoundary::OffsetFilter::kValidOffsets)
                     .valueOr(0)
               : 0;
  }
  [[nodiscard]] nsIContent* GetChildAtAnchorOffset() const {
    return AnchorRef().IsSet() ? AnchorRef().GetChildAtOffset() : nullptr;
  }

  [[nodiscard]] const RangeBoundary& FocusRef() const {
    if (!mAnchorFocusRange) {
      static RangeBoundary sEmptyRangeBoundary;
      return sEmptyRangeBoundary;
    }
    return mDirection == nsDirection::eDirNext ? mAnchorFocusRange->EndRef()
                                               : mAnchorFocusRange->StartRef();
  }
  [[nodiscard]] nsINode* GetFocusNode() const {
    return FocusRef().IsSet() ? FocusRef().GetContainer() : nullptr;
  }
  [[nodiscard]] uint32_t FocusOffset() const {
    return FocusRef().IsSet()
               ? FocusRef()
                     .Offset(RangeBoundary::OffsetFilter::kValidOffsets)
                     .valueOr(0)
               : 0;
  }
  [[nodiscard]] nsIContent* GetChildAtFocusOffset() const {
    return FocusRef().IsSet() ? FocusRef().GetChildAtOffset() : nullptr;
  }

  void RemoveAllRanges() {
    mRanges.Clear();
    mAnchorFocusRange = nullptr;
    mDirection = nsDirection::eDirNext;
  }

  enum class PreservePrimaryRangeIfAllRangesCollapsed : bool { No, Yes };
  void RemoveCollapsedRanges(PreservePrimaryRangeIfAllRangesCollapsed);

  /**
   * If the points are same (i.e., mean a collapsed range) and in an empty block
   * element except the padding <br> element, this makes aStartPoint and
   * aEndPoint contain the padding <br> element.
   */
  static void UpdatePointsToSelectAllChildrenIfCollapsedInEmptyBlockElement(
      EditorDOMPoint& aStartPoint, EditorDOMPoint& aEndPoint,
      const dom::Element& aEditingHost);

  /**
   * CreateRangeExtendedToHardLineStartAndEnd() creates a dom::Range
   * instance which may be expanded to start/end of hard line at both edges of
   * the given range.  If this fails handling something, returns nullptr.
   */
  static already_AddRefed<dom::Range>
  CreateRangeWrappingStartAndEndLinesContainingBoundaries(
      const EditorDOMRange& aRange, EditSubAction aEditSubAction,
      BlockInlineCheck aBlockInlineCheck, const dom::Element& aEditingHost) {
    if (!aRange.IsPositioned()) {
      return nullptr;
    }
    return CreateRangeWrappingStartAndEndLinesContainingBoundaries(
        aRange.StartRef(), aRange.EndRef(), aEditSubAction, aBlockInlineCheck,
        aEditingHost);
  }
  static already_AddRefed<dom::Range>
  CreateRangeWrappingStartAndEndLinesContainingBoundaries(
      const EditorDOMPoint& aStartPoint, const EditorDOMPoint& aEndPoint,
      EditSubAction aEditSubAction, BlockInlineCheck aBlockInlineCheck,
      const dom::Element& aEditingHost) {
    RefPtr<dom::Range> range =
        dom::Range::Create(aStartPoint.ToRawRangeBoundary(),
                           aEndPoint.ToRawRangeBoundary(), IgnoreErrors());
    if (MOZ_UNLIKELY(!range)) {
      return nullptr;
    }
    if (NS_FAILED(ExtendRangeToWrapStartAndEndLinesContainingBoundaries(
            *range, aEditSubAction, aBlockInlineCheck, aEditingHost)) ||
        MOZ_UNLIKELY(!range->IsPositioned())) {
      return nullptr;
    }
    return range.forget();
  }

  /**
   * Splits text nodes if each range end is in middle of a text node, then,
   * calls HTMLEditor::SplitParentInlineElementsAtRangeBoundaries() for each
   * range.  Finally, updates ranges to keep edit target ranges as expected.
   *
   * @param aHTMLEditor         The HTMLEditor which will handle the splittings.
   * @param aBlockInlineCheck   Considering block vs inline with whether the
   *                            computed style or the HTML default style.
   * @param aElement            The editing host.
   * @param aAncestorLimiter    A content node which you don't want this to
   *                            split it.
   * @return                    A suggest point to put caret if succeeded, but
   *                            it may be unset.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<EditorDOMPoint, nsresult>
  SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
      HTMLEditor& aHTMLEditor, BlockInlineCheck aBlockInlineCheck,
      const dom::Element& aEditingHost,
      const nsIContent* aAncestorLimiter = nullptr);

  /**
   * CollectEditTargetNodes() collects edit target nodes the ranges.
   * First, this collects all nodes in given ranges, then, modifies the
   * result for specific edit sub-actions.
   */
  enum class CollectNonEditableNodes { No, Yes };
  nsresult CollectEditTargetNodes(
      const HTMLEditor& aHTMLEditor,
      nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents,
      EditSubAction aEditSubAction,
      CollectNonEditableNodes aCollectNonEditableNodes) const;

  /**
   * Retrieve a closest ancestor list element of a common ancestor of _A_ range
   * of the ranges.  This tries to retrieve it from the first range to the last
   * range.
   */
  dom::Element* GetClosestAncestorAnyListElementOfRange() const;

  [[nodiscard]] virtual bool HasSavedRanges() const { return false; }

  /**
   * Extend all ranges to contain surrounding invisible white-spaces if there
   * are.
   *
   * @param aStripWrappers      nsIEditor::eStrip if the caller wants to delete
   *                            inline ancestors too.
   */
  void ExtendRangeToContainSurroundingInvisibleWhiteSpaces(
      nsIEditor::EStripWrappers aStripWrappers);

 protected:
  AutoClonedRangeArray() = default;

  static nsresult ExtendRangeToWrapStartAndEndLinesContainingBoundaries(
      dom::Range& aRange, EditSubAction aEditSubAction,
      BlockInlineCheck aBlockInlineCheck, const dom::Element& aEditingHost);

  using InterlinePosition = dom::Selection::InterlinePosition;
  virtual void SetNewCaretAssociationHint(
      const RawRangeBoundary& aPoint, InterlinePosition aInterlinePosition) {}

  AutoTArray<mozilla::OwningNonNull<dom::Range>, 8> mRanges;
  RefPtr<dom::Range> mAnchorFocusRange;
  nsDirection mDirection = nsDirection::eDirNext;
};

/******************************************************************************
 * AutoClonedSelectionRangeArray stores closed ranges and have similar API of
 * Selection.  So, different from `AutoSelectionRangeArray`, this can be used
 * for ranges which may need to be modified before touching the DOM tree, but
 * does not want to modify `Selection` for the performance.
 *****************************************************************************/

class MOZ_STACK_CLASS AutoClonedSelectionRangeArray final
    : public AutoClonedRangeArray {
  using Selection = dom::Selection;

 public:
  AutoClonedSelectionRangeArray() = delete;
  explicit AutoClonedSelectionRangeArray(const Selection& aSelection);
  template <typename PointType>
  AutoClonedSelectionRangeArray(
      const EditorDOMRangeBase<PointType>& aRange,
      const LimitersAndCaretData& aLimitersAndCaretData);
  template <typename PT, typename CT>
  AutoClonedSelectionRangeArray(
      const EditorDOMPointBase<PT, CT>& aPoint,
      const LimitersAndCaretData& aLimitersAndCaretData);
  AutoClonedSelectionRangeArray(
      const dom::Range& aRange,
      const LimitersAndCaretData& aLimitersAndCaretData);
  // The copy constructor copies everything except saved ranges.
  explicit AutoClonedSelectionRangeArray(
      const AutoClonedSelectionRangeArray& aOther);

  ~AutoClonedSelectionRangeArray() override {
    if (HasSavedRanges()) {
      ClearSavedRanges();
    }
  }

  void Initialize(const Selection& aSelection) {
    ClearSavedRanges();
    mDirection = aSelection.GetDirection();
    mRanges.Clear();
    if (nsFrameSelection* frameSelection = aSelection.GetFrameSelection()) {
      mLimitersAndCaretData = LimitersAndCaretData(*frameSelection);
    }
    for (const uint32_t i : IntegerRange(aSelection.RangeCount())) {
      MOZ_ASSERT(aSelection.GetRangeAt(i));
      const dom::Range* const range = aSelection.GetRangeAt(i);
      if (!RangeIsInLimiters(*range)) {
        continue;
      }
      mRanges.AppendElement(range->CloneRange());
      if (range == aSelection.GetAnchorFocusRange()) {
        mAnchorFocusRange = mRanges.LastElement();
      }
    }
  }

  /**
   * APIs to store ranges with only container node and offset in it, and track
   * them with RangeUpdater.
   */
  [[nodiscard]] bool SaveAndTrackRanges(HTMLEditor& aHTMLEditor);
  [[nodiscard]] bool HasSavedRanges() const override {
    return mSavedRanges.isSome();
  }
  void ClearSavedRanges();
  void RestoreFromSavedRanges() {
    MOZ_DIAGNOSTIC_ASSERT(mSavedRanges.isSome());
    if (MOZ_UNLIKELY(mSavedRanges.isNothing())) {
      return;
    }
    mSavedRanges->ApplyTo(*this);
    ClearSavedRanges();
  }

  /**
   * Apply mRanges and mDirection to aSelection.
   */
  MOZ_CAN_RUN_SCRIPT nsresult ApplyTo(dom::Selection& aSelection) {
    dom::SelectionBatcher selectionBatcher(aSelection, __FUNCTION__);
    aSelection.RemoveAllRanges(IgnoreErrors());
    MOZ_ASSERT(!aSelection.RangeCount());
    aSelection.SetDirection(mDirection);
    IgnoredErrorResult error;
    for (const OwningNonNull<dom::Range>& range : mRanges) {
      // MOZ_KnownLive(range) due to bug 1622253
      aSelection.AddRangeAndSelectFramesAndNotifyListeners(MOZ_KnownLive(range),
                                                           error);
      if (MOZ_UNLIKELY(error.Failed())) {
        return error.StealNSResult();
      }
    }
    // FIXME: If and only if the interline position is set explicitly, we need
    // to call `Selection::SetInterlinePosition` here with the specified value.
    // However, currently nobody does it.
    return NS_OK;
  }

  [[nodiscard]] const LimitersAndCaretData& LimitersAndCaretDataRef() const {
    return mLimitersAndCaretData;
  }

  /**
   * Equivalent to nsFrameSelection::GetIndependentSelectionRootElement().
   * NOTE: This should be called only when IsForSelection() returns true.
   */
  [[nodiscard]] dom::Element* GetIndependentSelectionRootElement() const {
    return mLimitersAndCaretData.mIndependentSelectionRootElement;
  }
  /**
   * Equivalent to nsFrameSelection::GetAncestorLimiter()
   * NOTE: This should be called only when IsForSelection() returns true.
   */
  [[nodiscard]] dom::Element* GetAncestorLimiter() const {
    return mLimitersAndCaretData.mAncestorLimiter;
  }
  /**
   * Equivalent to nsFrameSelection::GetHint(), this may be updated when
   * Collapse() is called.  Otherwise, this value may mismatch with what
   * nsFrameSelection returns if you worked with nsFrameSelection and
   * Selection directly.
   */
  [[nodiscard]] CaretAssociationHint GetHint() const {
    return mLimitersAndCaretData.mCaretAssociationHint;
  }
  /**
   * Equivalent to nsFrameSelection::GetCaretBidiLevel(), this returns the value
   * when this is initialized with nsFrameSelection or inherits another
   * instance.  Therefore, the value may be invalid once you already modified
   * the ranges.
   */
  [[nodiscard]] intl::BidiEmbeddingLevel GetCaretBidiLevel() const {
    return mLimitersAndCaretData.mCaretBidiLevel;
  }

  void SetAncestorLimiter(const dom::Element* aSelectionAncestorLimiter) {
    if (mLimitersAndCaretData.mAncestorLimiter == aSelectionAncestorLimiter) {
      return;
    }
    mLimitersAndCaretData.mAncestorLimiter =
        const_cast<dom::Element*>(aSelectionAncestorLimiter);
    if (NodeIsInLimiters(GetFocusNode())) {
      return;
    }
    RemoveAllRanges();
  }

  void SetInterlinePosition(Selection::InterlinePosition aInterlinePosition) {
    switch (aInterlinePosition) {
      case Selection::InterlinePosition::EndOfLine:
        mLimitersAndCaretData.mCaretAssociationHint =
            CaretAssociationHint::Before;
        break;
      case Selection::InterlinePosition::StartOfNextLine:
        mLimitersAndCaretData.mCaretAssociationHint =
            CaretAssociationHint::After;
        break;
      case Selection::InterlinePosition::Undefined:
        break;
    }
  }

  void SetCaretBidiLevel(intl::BidiEmbeddingLevel aBidiLevel) {
    mLimitersAndCaretData.mCaretBidiLevel = aBidiLevel;
  }

  [[nodiscard]] bool NodeIsInLimiters(const nsINode* aContainerNode) const {
    return mLimitersAndCaretData.NodeIsInLimiters(aContainerNode);
  }
  [[nodiscard]] bool RangeIsInLimiters(const dom::AbstractRange& aRange) const {
    return mLimitersAndCaretData.RangeInLimiters(aRange);
  }

  /**
   * ExtendAnchorFocusRangeFor() extends the anchor-focus range for deleting
   * content for aDirectionAndAmount.  The range won't be extended to outer of
   * selection limiter.  Note that if a range is extened, the range is
   * recreated.  Therefore, caller cannot cache pointer of any ranges before
   * calling this.
   * NOTE: When the range cannot be extened due to there is no more visible
   * content around the limiter, this returns error. Unfortunately, we cannot
   * distinguish whether the error is caused by unexpected situation or not.
   */
  [[nodiscard]] MOZ_CAN_RUN_SCRIPT Result<nsIEditor::EDirection, nsresult>
  ExtendAnchorFocusRangeFor(const EditorBase& aEditorBase,
                            nsIEditor::EDirection aDirectionAndAmount);

  /**
   * MaybeExtendAnchorFocusRangeToDeleteAdjacentWhiteSpace() extends the
   * anchor-focus range to delete one visible white-space next to a word which
   * was selected by double-click, matching WebKit's macOS smart delete.
   * Prefer preceding white-space, otherwise delete following white-space
   * without crossing a paragraph boundary.  Do not extend a range which
   * already starts or ends with visible white-space.
   * Enabled by `editor.word_select.delete_space_after_doubleclick_selection`.
   *
   *   "one [two] three" -> "one[ two] three" -> "one three"
   *   "[one] two"       -> "[one ]two"       -> "two"
   *   "one [two]"       -> "one[ two]"       -> "one"
   *   "one ,[two] three" -> "one ,[two ]three" -> "one ,three"
   *   "one [two ]three" -> unchanged         -> "one three"
   */
  void MaybeExtendAnchorFocusRangeToDeleteAdjacentWhiteSpace(
      const EditorBase& aEditorBase);

 private:
  void SetNewCaretAssociationHint(
      const RawRangeBoundary& aRawRangeBoundary,
      InterlinePosition aInternlinePosition) override;

  Maybe<SelectionState> mSavedRanges;
  RefPtr<HTMLEditor> mTrackingHTMLEditor;
  LimitersAndCaretData mLimitersAndCaretData;
};

}  // namespace mozilla

#endif  // #ifndef AutoClonedRangeArray_h
