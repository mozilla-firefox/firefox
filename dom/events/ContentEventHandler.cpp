/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ContentEventHandler.h"

#include <algorithm>

#include "mozilla/Assertions.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/Maybe.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/PresShell.h"
#include "mozilla/RangeBoundary.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/SelectionMovementUtils.h"
#include "mozilla/TextComposition.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TextEvents.h"
#include "mozilla/ViewportUtils.h"
#include "mozilla/dom/CharacterDataBuffer.h"
#include "mozilla/dom/EditContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLBRElement.h"
#include "mozilla/dom/HTMLUnknownElement.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/StaticRange.h"
#include "mozilla/dom/Text.h"
#include "nsCOMPtr.h"
#include "nsCaret.h"
#include "nsContentUtils.h"
#include "nsCopySupport.h"
#include "nsElementTable.h"
#include "nsFocusManager.h"
#include "nsFontMetrics.h"
#include "nsFrameSelection.h"
#include "nsHTMLTags.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsQueryObject.h"
#include "nsTextFrame.h"

// Work around conflicting define in rpcndr.h
#if defined(small)
#  undef small
#endif  // defined(small)

namespace mozilla {

using namespace dom;
using namespace widget;

/******************************************************************/
/* ContentEventHandler::SimpleRangeBase                           */
/******************************************************************/
template <>
ContentEventHandler::SimpleRangeBase<
    RefPtr<nsINode>, RangeBoundary>::SimpleRangeBase() = default;

template <>
ContentEventHandler::SimpleRangeBase<nsINode*,
                                     RawRangeBoundary>::SimpleRangeBase()
    : mRoot(nullptr) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  mAssertNoGC.emplace();
#endif  // #ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
}

template <>
template <typename OtherNodeType, typename OtherRangeBoundaryType>
ContentEventHandler::SimpleRangeBase<RefPtr<nsINode>, RangeBoundary>::
    SimpleRangeBase(
        const SimpleRangeBase<OtherNodeType, OtherRangeBoundaryType>& aOther)
    : mRoot(aOther.GetRoot()),
      mStart{aOther.Start().AsRaw()},
      mEnd{aOther.End().AsRaw()}
// Don't use the copy constructor of mAssertNoGC
{}

template <>
template <typename OtherNodeType, typename OtherRangeBoundaryType>
ContentEventHandler::SimpleRangeBase<nsINode*, RawRangeBoundary>::
    SimpleRangeBase(
        const SimpleRangeBase<OtherNodeType, OtherRangeBoundaryType>& aOther)
    : mRoot(aOther.GetRoot()),
      mStart{aOther.Start().AsRaw()},
      mEnd{aOther.End().AsRaw()} {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  mAssertNoGC.emplace();
#endif  // #ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
}

template <>
ContentEventHandler::SimpleRangeBase<RefPtr<nsINode>, RangeBoundary>::
    SimpleRangeBase(
        SimpleRangeBase<RefPtr<nsINode>, RangeBoundary>&& aOther) noexcept
    : mRoot(std::move(aOther.GetRoot())),
      mStart(std::move(aOther.mStart)),
      mEnd(std::move(aOther.mEnd)) {}

template <>
ContentEventHandler::SimpleRangeBase<nsINode*, RawRangeBoundary>::
    SimpleRangeBase(
        SimpleRangeBase<nsINode*, RawRangeBoundary>&& aOther) noexcept
    : mRoot(std::move(aOther.GetRoot())),
      mStart(std::move(aOther.mStart)),
      mEnd(std::move(aOther.mEnd)) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  mAssertNoGC.emplace();
#endif  // #ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
}

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
template <>
ContentEventHandler::SimpleRangeBase<
    RefPtr<nsINode>, RangeBoundary>::~SimpleRangeBase() = default;

template <>
ContentEventHandler::SimpleRangeBase<nsINode*,
                                     RawRangeBoundary>::~SimpleRangeBase() {
  MOZ_DIAGNOSTIC_ASSERT(!mMutationGuard.Mutated(0));
}
#endif  // #ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED

template <typename NodeType, typename RangeBoundaryType>
void ContentEventHandler::SimpleRangeBase<
    NodeType, RangeBoundaryType>::AssertStartIsBeforeOrEqualToEnd() {
  MOZ_ASSERT(*nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(
                 mStart, mEnd) <= 0);
}

template <typename NodeType, typename RangeBoundaryType>
nsresult
ContentEventHandler::SimpleRangeBase<NodeType, RangeBoundaryType>::SetStart(
    const RawRangeBoundary& aStart) {
  nsINode* newRoot = RangeUtils::ComputeRootNode(aStart.GetContainer());
  if (!newRoot) {
    return NS_ERROR_DOM_INVALID_NODE_TYPE_ERR;
  }

  if (!aStart.IsSetAndValid()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  // Collapse if not positioned yet, or if positioned in another document.
  if (!IsPositioned() || newRoot != mRoot) {
    mRoot = newRoot;
    mStart.CopyFrom(aStart, RangeBoundarySetBy::Ref);
    mEnd.CopyFrom(aStart, RangeBoundarySetBy::Ref);
    return NS_OK;
  }

  mStart.CopyFrom(aStart, RangeBoundarySetBy::Ref);
  AssertStartIsBeforeOrEqualToEnd();
  return NS_OK;
}

template <typename NodeType, typename RangeBoundaryType>
nsresult
ContentEventHandler::SimpleRangeBase<NodeType, RangeBoundaryType>::SetEnd(
    const RawRangeBoundary& aEnd) {
  nsINode* newRoot = RangeUtils::ComputeRootNode(aEnd.GetContainer());
  if (!newRoot) {
    return NS_ERROR_DOM_INVALID_NODE_TYPE_ERR;
  }

  if (!aEnd.IsSetAndValid()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  // Collapse if not positioned yet, or if positioned in another document.
  if (!IsPositioned() || newRoot != mRoot) {
    mRoot = newRoot;
    mStart.CopyFrom(aEnd, RangeBoundarySetBy::Ref);
    mEnd.CopyFrom(aEnd, RangeBoundarySetBy::Ref);
    return NS_OK;
  }

  mEnd.CopyFrom(aEnd, RangeBoundarySetBy::Ref);
  AssertStartIsBeforeOrEqualToEnd();
  return NS_OK;
}

template <typename NodeType, typename RangeBoundaryType>
nsresult
ContentEventHandler::SimpleRangeBase<NodeType, RangeBoundaryType>::SetEndAfter(
    nsIContent* aEndContainer) {
  return SetEnd(RawRangeBoundary::After(*aEndContainer));
}

template <typename NodeType, typename RangeBoundaryType>
void ContentEventHandler::SimpleRangeBase<
    NodeType, RangeBoundaryType>::SetStartAndEnd(const dom::Range* aRange) {
  DebugOnly<nsresult> rv =
      SetStartAndEnd(aRange->StartRef().AsRaw(), aRange->EndRef().AsRaw());
  MOZ_ASSERT(!aRange->IsPositioned() || NS_SUCCEEDED(rv));
}

template <typename NodeType, typename RangeBoundaryType>
nsresult ContentEventHandler::SimpleRangeBase<
    NodeType, RangeBoundaryType>::SetStartAndEnd(const RawRangeBoundary& aStart,
                                                 const RawRangeBoundary& aEnd) {
  nsINode* newStartRoot = RangeUtils::ComputeRootNode(aStart.GetContainer());
  if (!newStartRoot) {
    return NS_ERROR_DOM_INVALID_NODE_TYPE_ERR;
  }
  if (!aStart.IsSetAndValid()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  if (aStart.GetContainer() == aEnd.GetContainer()) {
    if (!aEnd.IsSetAndValid()) {
      return NS_ERROR_DOM_INDEX_SIZE_ERR;
    }
    MOZ_ASSERT(*aStart.Offset(RawRangeBoundary::OffsetFilter::kValidOffsets) <=
               *aEnd.Offset(RawRangeBoundary::OffsetFilter::kValidOffsets));
    mRoot = newStartRoot;
    mStart.CopyFrom(aStart, RangeBoundarySetBy::Ref);
    mEnd.CopyFrom(aEnd, RangeBoundarySetBy::Ref);
    return NS_OK;
  }

  nsINode* newEndRoot = RangeUtils::ComputeRootNode(aEnd.GetContainer());
  if (!newEndRoot) {
    return NS_ERROR_DOM_INVALID_NODE_TYPE_ERR;
  }
  if (!aEnd.IsSetAndValid()) {
    return NS_ERROR_DOM_INDEX_SIZE_ERR;
  }

  // If they have different root, this should be collapsed at the end point.
  if (newStartRoot != newEndRoot) {
    mRoot = newEndRoot;
    mStart.CopyFrom(aEnd, RangeBoundarySetBy::Ref);
    mEnd.CopyFrom(aEnd, RangeBoundarySetBy::Ref);
    return NS_OK;
  }

  // Otherwise, set the range as specified.
  mRoot = newStartRoot;
  mStart.CopyFrom(aStart, RangeBoundarySetBy::Ref);
  mEnd.CopyFrom(aEnd, RangeBoundarySetBy::Ref);
  AssertStartIsBeforeOrEqualToEnd();
  return NS_OK;
}

template <typename NodeType, typename RangeBoundaryType>
nsresult ContentEventHandler::SimpleRangeBase<NodeType, RangeBoundaryType>::
    SelectNodeContents(const nsINode* aNodeToSelectContents) {
  nsINode* const newRoot =
      RangeUtils::ComputeRootNode(const_cast<nsINode*>(aNodeToSelectContents));
  if (!newRoot) {
    return NS_ERROR_DOM_INVALID_NODE_TYPE_ERR;
  }
  mRoot = newRoot;
  mStart =
      RangeBoundaryType(const_cast<nsINode*>(aNodeToSelectContents), nullptr);
  mEnd = RangeBoundaryType(const_cast<nsINode*>(aNodeToSelectContents),
                           aNodeToSelectContents->GetLastChild());
  return NS_OK;
}

/******************************************************************/
/* ContentEventHandler                                            */
/******************************************************************/

// NOTE
//
// ContentEventHandler *creates* ranges as following rules:
// 1. Start of range:
//   1.1. Cases: [textNode or text[Node or textNode[
//        When text node is start of a range, start node is the text node and
//        start offset is any number between 0 and the length of the text.
//   1.2. Case: [<element>:
//        When start of an element node is start of a range, start node is
//        parent of the element and start offset is the element's index in the
//        parent.
//   1.3. Case: <element/>[
//        When after an empty element node is start of a range, start node is
//        parent of the element and start offset is the element's index in the
//        parent + 1.
//   1.4. Case: <element>[
//        When start of a non-empty element is start of a range, start node is
//        the element and start offset is 0.
//   1.5. Case: <root>[
//        When start of a range is 0 and there are no nodes causing text,
//        start node is the root node and start offset is 0.
//   1.6. Case: [</root>
//        When start of a range is out of bounds, start node is the root node
//        and start offset is number of the children.
// 2. End of range:
//   2.1. Cases: ]textNode or text]Node or textNode]
//        When a text node is end of a range, end node is the text node and
//        end offset is any number between 0 and the length of the text.
//   2.2. Case: ]<element>
//        When before an element node (meaning before the open tag of the
//        element) is end of a range, end node is previous node causing text.
//        Note that this case shouldn't be handled directly.  If rule 2.1 and
//        2.3 are handled correctly, the loop with ContentIterator shouldn't
//        reach the element node since the loop should've finished already at
//        handling the last node which caused some text.
//   2.3. Case: <element>]
//        When a line break is caused before a non-empty element node and it's
//        end of a range, end node is the element and end offset is 0.
//        (i.e., including open tag of the element)
//   2.4. Cases: <element/>]
//        When after an empty element node is end of a range, end node is
//        parent of the element node and end offset is the element's index in
//        the parent + 1.  (i.e., including close tag of the element or empty
//        element)
//   2.5. Case: ]</root>
//        When end of a range is out of bounds, end node is the root node and
//        end offset is number of the children.
//
// ContentEventHandler *treats* ranges as following additional rules:
// 1. When the start node is an element node which doesn't have children,
//    it includes a line break caused before itself (i.e., includes its open
//    tag).  For example, if start position is { <br>, 0 }, the line break
//    caused by <br> should be included into the flatten text.
// 2. When the end node is an element node which doesn't have children,
//    it includes the end (i.e., includes its close tag except empty element).
//    Although, currently, any close tags don't cause line break, this also
//    includes its open tag.  For example, if end position is { <br>, 0 }, the
//    line break caused by the <br> should be included into the flatten text.

ContentEventHandler::ContentEventHandler(nsPresContext* aPresContext)
    : mDocument(aPresContext->Document()) {}

nsresult ContentEventHandler::InitBasic(bool aRequireFlush) {
  NS_ENSURE_TRUE(mDocument, NS_ERROR_NOT_AVAILABLE);
  if (aRequireFlush) {
    // If text frame which has overflowing selection underline is dirty,
    // we need to flush the pending reflow here.
    mDocument->FlushPendingNotifications(FlushType::Layout);
  }
  return NS_OK;
}

Result<dom::Range*, nsresult> ContentEventHandler::InitRootContent(
    const Selection& aNormalSelection) {
  // Root content should be computed with normal selection because normal
  // selection is typically has at least one range but the other selections
  // not so.  If there is a range, computing its root is easy, but if
  // there are no ranges, we need to use ancestor limit instead.
  MOZ_ASSERT(aNormalSelection.Type() == SelectionType::eNormal);

  EditContext* editContext = mDocument->GetActiveEditContext();

  auto getRangeInRootElement = [&]() MOZ_NEVER_INLINE_DEBUG -> dom::Range* {
    nsFrameSelection* const fs = aNormalSelection.GetFrameSelection();
    if (NS_WARN_IF(!fs)) {
      return nullptr;
    }
    for (const uint32_t i : IntegerRange(aNormalSelection.RangeCount())) {
      dom::Range* const range = aNormalSelection.GetRangeAt(i);
      MOZ_ASSERT(range);
      if (fs->RangeInLimiters(*range)) {
        return range;
      }
      // For EditContext, it's acceptable to have the selection outside the
      // host element.
      if (!editContext) {
        NS_WARNING(fmt::format("{} (index: {}) is not in the limiters {}",
                               RefPtr{range}, i, fs->LimitersRef())
                       .c_str());
      }
    }
    return nullptr;
  };

  // Always use the active EditContext if there is one, regardless of the
  // selection.
  if (editContext) {
    MOZ_ASSERT(editContext->GetAssociatedElement(),
               "Should always have an associated element if active.");
    mRootElement = editContext->GetAssociatedElement();
    return getRangeInRootElement();
  }

  const auto SetRootElementWithNoRanges =
      [&]() -> Result<dom::Range*, nsresult> {
    // If there is no selection range, we should compute the selection root
    // from ancestor limiter or root content of the document.
    mRootElement = aNormalSelection.GetAncestorLimiter();
    if (!mRootElement) {
      mRootElement = mDocument->GetRootElement();
      if (NS_WARN_IF(!mRootElement)) {
        return Err(NS_ERROR_NOT_AVAILABLE);
      }
    }
    // See bug 2046677. If the ancestor limiter is moved to another document, we
    // cannot handle it.
    if (mRootElement->IsInComposedDoc() &&
        NS_WARN_IF(mRootElement->GetComposedDoc() !=
                   aNormalSelection.GetDocument())) [[unlikely]] {
      mRootElement = nullptr;
      return Err(NS_ERROR_FAILURE);
    }
    return nullptr;
  };

  if (!aNormalSelection.RangeCount()) {
    return SetRootElementWithNoRanges();
  }

  // See bug 2046677. The range may be outside the ancestor limiter if it was
  // removed from the DOM. Therefore, we should ignore "invalid" ranges.
  dom::Range* const rangeInRootElement = getRangeInRootElement();
  if (!rangeInRootElement) {
    return SetRootElementWithNoRanges();
  }

  // If there is a selection, we should retrieve the selection root from
  // the range since when the window is inactivated, the ancestor limiter
  // of selection was cleared by blur event handler of EditorBase but the
  // selection range still keeps storing the nodes.  If the active element of
  // the deactive window is <input> or <textarea>, we can compute the
  // selection root from them.
  nsINode* const startNode = rangeInRootElement->GetStartContainer();
  nsINode* const endNode = rangeInRootElement->GetEndContainer();
  if (NS_WARN_IF(!startNode) || NS_WARN_IF(!endNode)) {
    return Err(NS_ERROR_FAILURE);
  }

  // See bug 537041 comment 5, the range could have removed node.
  if (NS_WARN_IF(startNode->GetComposedDoc() != mDocument)) {
    return Err(NS_ERROR_FAILURE);
  }

  NS_ASSERTION(startNode->GetComposedDoc() == endNode->GetComposedDoc(),
               "firstNormalSelectionRange crosses the document boundary");

  mRootElement = Element::FromNodeOrNull(startNode->GetSelectionRootContent(
      mDocument->GetPresShell(), nsINode::IgnoreOwnIndependentSelection::Yes,
      nsINode::AllowCrossShadowBoundary::No));
  if (NS_WARN_IF(!mRootElement)) {
    return Err(NS_ERROR_FAILURE);
  }
  return rangeInRootElement;
}

nsresult ContentEventHandler::InitCommon(EventMessage aEventMessage,
                                         SelectionType aSelectionType,
                                         bool aRequireFlush) {
  if (mSelection && mSelection->Type() == aSelectionType) {
    return NS_OK;
  }

  mSelection = nullptr;
  mRootElement = nullptr;
  mFirstSelectedSimpleRange.Clear();

  nsresult rv = InitBasic(aRequireFlush);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsFrameSelection> frameSel;
  if (PresShell* presShell = mDocument->GetPresShell()) {
    frameSel = presShell->GetLastFocusedFrameSelection();
  }
  if (NS_WARN_IF(!frameSel)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mSelection = frameSel->GetSelection(aSelectionType);
  if (NS_WARN_IF(!mSelection)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<Selection> normalSelection;
  if (mSelection->Type() == SelectionType::eNormal) {
    normalSelection = mSelection;
  } else {
    normalSelection = &frameSel->NormalSelection();
    MOZ_ASSERT(normalSelection);
  }

  Result<RefPtr<dom::Range>, nsresult> firstRangeOrError =
      InitRootContent(*normalSelection);
  if (NS_WARN_IF(firstRangeOrError.isErr())) {
    return firstRangeOrError.unwrapErr();
  }

  if (mSelection->Type() == SelectionType::eNormal) {
    if (firstRangeOrError.inspect()) {
      mFirstSelectedSimpleRange.SetStartAndEnd(firstRangeOrError.inspect());
      return NS_OK;
    }
    // Let's return no-range case if we're handling a selection range.
    if (aEventMessage == eQuerySelectedText) {
      return NS_OK;
    }
  } else {
    if (mSelection->RangeCount()) {
      mFirstSelectedSimpleRange.SetStartAndEnd(mSelection->GetRangeAt(0));
      return NS_OK;
    }
    // Even if there is no selection range, it's usual case if aSelectionType
    // is a special selection.
    return NS_OK;
  }

  // But otherwise, we need to assume that there is a selection range at the
  // beginning of the root content if aSelectionType is eNormal.
  rv = mFirstSelectedSimpleRange.CollapseTo(
      RawRangeBoundary::StartOfParent(*mRootElement));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

nsresult ContentEventHandler::Init(WidgetQueryContentEvent* aEvent) {
  NS_ASSERTION(aEvent, "aEvent must not be null");
  MOZ_ASSERT(aEvent->mMessage == eQuerySelectedText ||
             aEvent->mInput.mSelectionType == SelectionType::eNormal);

  if (NS_WARN_IF(!aEvent->mInput.IsValidOffset()) ||
      NS_WARN_IF(!aEvent->mInput.IsValidEventMessage(aEvent->mMessage))) {
    return NS_ERROR_FAILURE;
  }

  // Note that we should ignore WidgetQueryContentEvent::Input::mSelectionType
  // if the event isn't eQuerySelectedText.
  SelectionType selectionType = aEvent->mMessage == eQuerySelectedText
                                    ? aEvent->mInput.mSelectionType
                                    : SelectionType::eNormal;
  if (NS_WARN_IF(selectionType == SelectionType::eNone)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = InitCommon(aEvent->mMessage, selectionType,
                           aEvent->AllowFlushingPendingNotifications());
  NS_ENSURE_SUCCESS(rv, rv);

  // Be aware, WidgetQueryContentEvent::mInput::mOffset should be made absolute
  // offset before sending it to ContentEventHandler because querying selection
  // every time may be expensive.  So, if the caller caches selection, it
  // should initialize the event with the cached value.
  if (aEvent->mInput.mRelativeToInsertionPoint) {
    MOZ_ASSERT(selectionType == SelectionType::eNormal);
    TextComposition* composition =
        IMEStateManager::GetTextCompositionFor(aEvent->mWidget);
    if (composition) {
      uint32_t compositionStart = composition->NativeOffsetOfStartComposition();
      if (NS_WARN_IF(!aEvent->mInput.MakeOffsetAbsolute(compositionStart))) {
        return NS_ERROR_FAILURE;
      }
    } else {
      const Result<uint32_t, nsresult> selectionStartOrError =
          GetStartOffset(mFirstSelectedSimpleRange);
      if (NS_WARN_IF(selectionStartOrError.isErr())) {
        return NS_ERROR_FAILURE;
      }
      if (NS_WARN_IF(!aEvent->mInput.MakeOffsetAbsolute(
              selectionStartOrError.inspect()))) {
        return NS_ERROR_FAILURE;
      }
    }
  }

  // Ideally, we should emplace only when we return succeeded event.
  // However, we need to emplace here since it's hard to store the various
  // result.  Intead, `HandleQueryContentEvent()` will reset `mReply` if
  // corresponding handler returns error.
  aEvent->EmplaceReply();

  aEvent->mReply->mContentsRoot = mRootElement.get();
  aEvent->mReply->mIsEditableContent =
      mRootElement && mRootElement->IsEditable();

  nsRect r;
  nsIFrame* frame = nsCaret::GetGeometry(mSelection, &r);
  if (!frame) {
    frame = mRootElement->GetPrimaryFrame();
    if (NS_WARN_IF(!frame)) {
      return NS_ERROR_FAILURE;
    }
  }
  aEvent->mReply->mFocusedWidget = frame->GetNearestWidget();

  return NS_OK;
}

nsresult ContentEventHandler::Init(WidgetSelectionEvent* aEvent) {
  NS_ASSERTION(aEvent, "aEvent must not be null");

  nsresult rv = InitCommon(aEvent->mMessage);
  NS_ENSURE_SUCCESS(rv, rv);

  aEvent->mSucceeded = false;

  return NS_OK;
}

nsIContent* ContentEventHandler::GetFocusedContent() {
  nsCOMPtr<nsPIDOMWindowOuter> window = mDocument->GetWindow();
  nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
  return nsFocusManager::GetFocusedDescendant(
      window, nsFocusManager::eIncludeAllDescendants,
      getter_AddRefs(focusedWindow));
}

nsresult ContentEventHandler::QueryContentRect(
    nsIContent* aContent, WidgetQueryContentEvent* aEvent) {
  MOZ_ASSERT(aContent, "aContent must not be null");

  nsIFrame* frame = aContent->GetPrimaryFrame();
  NS_ENSURE_TRUE(frame, NS_ERROR_FAILURE);

  // get rect for first frame
  nsRect resultRect(nsPoint(0, 0), frame->GetRect().Size());
  nsresult rv = ConvertToRootRelativeOffset(frame, resultRect);
  NS_ENSURE_SUCCESS(rv, rv);

  nsPresContext* presContext = frame->PresContext();

  // account for any additional frames
  while ((frame = frame->GetNextContinuation())) {
    nsRect frameRect(nsPoint(0, 0), frame->GetRect().Size());
    rv = ConvertToRootRelativeOffset(frame, frameRect);
    NS_ENSURE_SUCCESS(rv, rv);
    resultRect.UnionRect(resultRect, frameRect);
  }

  aEvent->mReply->mRect = LayoutDeviceIntRect::FromAppUnitsToOutside(
      resultRect, presContext->AppUnitsPerDevPixel());
  // Returning empty rect may cause native IME confused, let's make sure to
  // return non-empty rect.
  EnsureNonEmptyRect(aEvent->mReply->mRect);

  return NS_OK;
}

// Editor places a padding <br> element under its root content if the editor
// doesn't have any text. This happens even for single line editors.
// When we get text content and when we change the selection,
// we don't want to include the padding <br> elements at the end.
static bool IsContentBR(const nsIContent& aContent) {
  const HTMLBRElement* brElement = HTMLBRElement::FromNode(aContent);
  return brElement && !brElement->IsPaddingForEmptyLastLine() &&
         !brElement->IsPaddingForEmptyEditor();
}

static bool IsPaddingBR(const nsIContent& aContent) {
  return aContent.IsHTMLElement(nsGkAtoms::br) && !IsContentBR(aContent);
}

static void AppendString(nsString& aString, const Text& aTextNode) {
  const uint32_t oldXPLength = aString.Length();
  aTextNode.DataBuffer().AppendTo(aString);
  if (aTextNode.HasFlag(NS_MAYBE_MASKED)) {
    TextEditor::MaskString(aString, aTextNode, oldXPLength, 0);
  }
}

static void AppendSubString(nsString& aString, const Text& aTextNode,
                            uint32_t aXPOffset, uint32_t aXPLength) {
  const uint32_t oldXPLength = aString.Length();
  aTextNode.DataBuffer().AppendTo(aString, aXPOffset, aXPLength);
  if (aTextNode.HasFlag(NS_MAYBE_MASKED)) {
    TextEditor::MaskString(aString, aTextNode, oldXPLength, aXPOffset);
  }
}

/* static */
uint32_t ContentEventHandler::GetNativeTextLength(const Text& aTextNode,
                                                  uint32_t aStartOffset,
                                                  uint32_t aEndOffset) {
  MOZ_ASSERT(aEndOffset >= aStartOffset,
             "aEndOffset must be equals or larger than aStartOffset");
  if (aStartOffset == aEndOffset) {
    return 0;
  }
  return GetTextLength(aTextNode, aEndOffset) -
         GetTextLength(aTextNode, aStartOffset);
}

/* static */
uint32_t ContentEventHandler::GetNativeTextLength(const Text& aTextNode,
                                                  uint32_t aMaxLength) {
  return GetTextLength(aTextNode, aMaxLength);
}

/* static */
uint32_t ContentEventHandler::GetTextLength(const Text& aTextNode,
                                            uint32_t aMaxLength) {
  return std::min(aTextNode.DataBuffer().GetLength(), aMaxLength);
}

/* static */
uint32_t ContentEventHandler::GetNativeTextLength(const nsAString& aText) {
  return aText.Length();
}

/* static */
bool ContentEventHandler::ShouldBreakLineBefore(const nsIContent& aContent,
                                                const Element* aRootElement) {
  // We don't need to append linebreak at the start of the root element.
  if (&aContent == aRootElement) {
    return false;
  }

  // If it's not an HTML element (including other markup language's elements),
  // we shouldn't insert like break before that for now.  Becoming this is a
  // problem must be edge case.  E.g., when ContentEventHandler is used with
  // MathML or SVG elements.
  if (!aContent.IsHTMLElement()) {
    return false;
  }

  switch (aContent.NodeInfo()->HTMLTag().valueOr(eHTMLTag_unknown)) {
    case eHTMLTag_br:
      // If the element is <br>, we need to check if the <br> is caused by web
      // content.  Otherwise, i.e., it's caused by internal reason of Gecko,
      // it shouldn't be exposed as a line break to flatten text.
      return IsContentBR(aContent);
    case eHTMLTag_a:
    case eHTMLTag_abbr:
    case eHTMLTag_acronym:
    case eHTMLTag_b:
    case eHTMLTag_bdi:
    case eHTMLTag_bdo:
    case eHTMLTag_big:
    case eHTMLTag_cite:
    case eHTMLTag_code:
    case eHTMLTag_data:
    case eHTMLTag_del:
    case eHTMLTag_dfn:
    case eHTMLTag_em:
    case eHTMLTag_font:
    case eHTMLTag_i:
    case eHTMLTag_ins:
    case eHTMLTag_kbd:
    case eHTMLTag_mark:
    case eHTMLTag_s:
    case eHTMLTag_samp:
    case eHTMLTag_small:
    case eHTMLTag_span:
    case eHTMLTag_strike:
    case eHTMLTag_strong:
    case eHTMLTag_sub:
    case eHTMLTag_sup:
    case eHTMLTag_time:
    case eHTMLTag_tt:
    case eHTMLTag_u:
    case eHTMLTag_var:
      // Note that ideally, we should refer the style of the primary frame of
      // aContent for deciding if it's an inline.  However, it's difficult
      // IMEContentObserver to notify IME of text change caused by style change.
      // Therefore, currently, we should check only from the tag for now.
      return false;
    case eHTMLTag_userdefined:
    case eHTMLTag_unknown:
      // If the element is unknown element, we shouldn't insert line breaks
      // before it since unknown elements should be ignored.
      return false;
    default:
      return true;
  }
}

nsresult ContentEventHandler::GenerateFlatTextContent(const Element* aElement,
                                                      nsString& aString) {
  MOZ_ASSERT(aString.IsEmpty());

  UnsafeSimpleRange rawRange;
  nsresult rv = rawRange.SelectNodeContents(aElement);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return GenerateFlatTextContent(rawRange, aString);
}

nsresult ContentEventHandler::GenerateFlatTextContent(const dom::Range* aRange,
                                                      nsString& aString) {
  MOZ_ASSERT(aString.IsEmpty());

  if (NS_WARN_IF(!aRange)) {
    return NS_ERROR_FAILURE;
  }

  UnsafeSimpleRange rawRange;
  rawRange.SetStartAndEnd(aRange);

  return GenerateFlatTextContent(rawRange, aString);
}

template <typename NodeType, typename RangeBoundaryType>
nsresult ContentEventHandler::GenerateFlatTextContent(
    const SimpleRangeBase<NodeType, RangeBoundaryType>& aSimpleRange,
    nsString& aString) {
  MOZ_ASSERT(aString.IsEmpty());

  if (aSimpleRange.Collapsed()) {
    return NS_OK;
  }

  nsINode* startNode = aSimpleRange.GetStartContainer();
  nsINode* endNode = aSimpleRange.GetEndContainer();
  if (NS_WARN_IF(!startNode) || NS_WARN_IF(!endNode)) {
    return NS_ERROR_FAILURE;
  }

  if (startNode == endNode && startNode->IsText()) {
    AppendSubString(aString, *startNode->AsText(), aSimpleRange.StartOffset(),
                    aSimpleRange.EndOffset() - aSimpleRange.StartOffset());
    return NS_OK;
  }

  UnsafePreContentIterator preOrderIter;
  nsresult rv = preOrderIter.Init(aSimpleRange.Start().AsRaw(),
                                  aSimpleRange.End().AsRaw());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  for (; !preOrderIter.IsDone(); preOrderIter.Next()) {
    nsINode* node = preOrderIter.GetCurrentNode();
    if (NS_WARN_IF(!node)) {
      break;
    }
    if (!node->IsContent()) {
      continue;
    }

    if (const Text* textNode = Text::FromNode(node)) {
      if (textNode == startNode) {
        AppendSubString(aString, *textNode, aSimpleRange.StartOffset(),
                        textNode->TextLength() - aSimpleRange.StartOffset());
      } else if (textNode == endNode) {
        AppendSubString(aString, *textNode, 0, aSimpleRange.EndOffset());
      } else {
        AppendString(aString, *textNode);
      }
    } else if (ShouldBreakLineBefore(*node->AsContent(), mRootElement)) {
      aString.Append(char16_t('\n'));
    }
  }
  return NS_OK;
}

static FontRange* AppendFontRange(nsTArray<FontRange>& aFontRanges,
                                  uint32_t aBaseOffset) {
  FontRange* fontRange = aFontRanges.AppendElement();
  fontRange->mStartOffset = aBaseOffset;
  return fontRange;
}

/* static */
uint32_t ContentEventHandler::GetTextLengthInRange(const Text& aTextNode,
                                                   uint32_t aXPStartOffset,
                                                   uint32_t aXPEndOffset) {
  return aXPEndOffset - aXPStartOffset;
}

/* static */
void ContentEventHandler::AppendFontRanges(FontRangeArray& aFontRanges,
                                           const Text& aTextNode,
                                           uint32_t aBaseOffset,
                                           uint32_t aXPStartOffset,
                                           uint32_t aXPEndOffset) {
  nsIFrame* frame = aTextNode.GetPrimaryFrame();
  if (!frame) {
    // It is a non-rendered content, create an empty range for it.
    AppendFontRange(aFontRanges, aBaseOffset);
    return;
  }

  uint32_t baseOffset = aBaseOffset;
#ifdef DEBUG
  {
    nsTextFrame* text = do_QueryFrame(frame);
    MOZ_ASSERT(text, "Not a text frame");
  }
#endif
  auto* curr = static_cast<nsTextFrame*>(frame);
  while (curr) {
    uint32_t frameXPStart = std::max(
        static_cast<uint32_t>(curr->GetContentOffset()), aXPStartOffset);
    uint32_t frameXPEnd =
        std::min(static_cast<uint32_t>(curr->GetContentEnd()), aXPEndOffset);
    if (frameXPStart >= frameXPEnd) {
      curr = curr->GetNextContinuation();
      continue;
    }

    gfxSkipCharsIterator iter = curr->EnsureTextRun(nsTextFrame::eInflated);
    gfxTextRun* textRun = curr->GetTextRun(nsTextFrame::eInflated);

    nsTextFrame* next = nullptr;
    if (frameXPEnd < aXPEndOffset) {
      next = curr->GetNextContinuation();
      while (next && next->GetTextRun(nsTextFrame::eInflated) == textRun) {
        frameXPEnd = std::min(static_cast<uint32_t>(next->GetContentEnd()),
                              aXPEndOffset);
        next =
            frameXPEnd < aXPEndOffset ? next->GetNextContinuation() : nullptr;
      }
    }

    gfxTextRun::Range skipRange(iter.ConvertOriginalToSkipped(frameXPStart),
                                iter.ConvertOriginalToSkipped(frameXPEnd));
    uint32_t lastXPEndOffset = frameXPStart;
    for (gfxTextRun::GlyphRunIterator runIter(textRun, skipRange);
         !runIter.AtEnd(); runIter.NextRun()) {
      gfxFont* font = runIter.GlyphRun()->mFont.get();
      uint32_t startXPOffset =
          iter.ConvertSkippedToOriginal(runIter.StringStart());
      // It is possible that the first glyph run has exceeded the frame,
      // because the whole frame is filled by skipped chars.
      if (startXPOffset >= frameXPEnd) {
        break;
      }

      if (startXPOffset > lastXPEndOffset) {
        // Create range for skipped leading chars.
        AppendFontRange(aFontRanges, baseOffset);
        baseOffset +=
            GetTextLengthInRange(aTextNode, lastXPEndOffset, startXPOffset);
      }

      FontRange* fontRange = AppendFontRange(aFontRanges, baseOffset);
      fontRange->mFontName.Append(NS_ConvertUTF8toUTF16(font->GetName()));

      ParentLayerToScreenScale2D cumulativeResolution =
          ParentLayerToParentLayerScale(
              frame->PresShell()->GetCumulativeResolution()) *
          nsLayoutUtils::GetTransformToAncestorScaleCrossProcessForFrameMetrics(
              frame);
      float scale =
          std::max(cumulativeResolution.xScale, cumulativeResolution.yScale);

      fontRange->mFontSize = font->GetAdjustedSize() * scale;

      // The converted original offset may exceed the range,
      // hence we need to clamp it.
      uint32_t endXPOffset = iter.ConvertSkippedToOriginal(runIter.StringEnd());
      endXPOffset = std::min(frameXPEnd, endXPOffset);
      baseOffset += GetTextLengthInRange(aTextNode, startXPOffset, endXPOffset);
      lastXPEndOffset = endXPOffset;
    }
    if (lastXPEndOffset < frameXPEnd) {
      // Create range for skipped trailing chars. It also handles case
      // that the whole frame contains only skipped chars.
      AppendFontRange(aFontRanges, baseOffset);
      baseOffset +=
          GetTextLengthInRange(aTextNode, lastXPEndOffset, frameXPEnd);
    }

    curr = next;
  }
}

nsresult ContentEventHandler::GenerateFlatFontRanges(
    const UnsafeSimpleRange& aSimpleRange, FontRangeArray& aFontRanges,
    uint32_t& aLength) {
  MOZ_ASSERT(aFontRanges.IsEmpty(), "aRanges must be empty array");

  if (aSimpleRange.Collapsed()) {
    return NS_OK;
  }

  nsINode* startNode = aSimpleRange.GetStartContainer();
  nsINode* endNode = aSimpleRange.GetEndContainer();
  if (NS_WARN_IF(!startNode) || NS_WARN_IF(!endNode)) {
    return NS_ERROR_FAILURE;
  }

  // baseOffset is the flattened offset of each content node.
  uint32_t baseOffset = 0;
  UnsafePreContentIterator preOrderIter;
  nsresult rv = preOrderIter.Init(aSimpleRange.Start().AsRaw(),
                                  aSimpleRange.End().AsRaw());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  for (; !preOrderIter.IsDone(); preOrderIter.Next()) {
    nsINode* node = preOrderIter.GetCurrentNode();
    if (NS_WARN_IF(!node)) {
      break;
    }
    if (!node->IsContent()) {
      continue;
    }
    nsIContent* content = node->AsContent();

    if (const Text* textNode = Text::FromNode(content)) {
      const uint32_t startOffset =
          textNode != startNode ? 0 : aSimpleRange.StartOffset();
      const uint32_t endOffset = textNode != endNode ? textNode->TextLength()
                                                     : aSimpleRange.EndOffset();
      AppendFontRanges(aFontRanges, *textNode, baseOffset, startOffset,
                       endOffset);
      baseOffset += GetTextLengthInRange(*textNode, startOffset, endOffset);
    } else if (ShouldBreakLineBefore(*content, mRootElement)) {
      if (aFontRanges.IsEmpty()) {
        MOZ_ASSERT(baseOffset == 0);
        FontRange* fontRange = AppendFontRange(aFontRanges, baseOffset);
        if (nsIFrame* frame = content->GetPrimaryFrame()) {
          const nsFont& font = frame->GetParent()->StyleFont()->mFont;
          const StyleFontFamilyList& fontList = font.family.families;
          MOZ_ASSERT(!fontList.list.IsEmpty(), "Empty font family?");
          const StyleSingleFontFamily* fontName =
              fontList.list.IsEmpty() ? nullptr : &fontList.list.AsSpan()[0];
          nsAutoCString name;
          if (fontName) {
            fontName->AppendToString(name, false);
          }
          AppendUTF8toUTF16(name, fontRange->mFontName);

          ParentLayerToScreenScale2D cumulativeResolution =
              ParentLayerToParentLayerScale(
                  frame->PresShell()->GetCumulativeResolution()) *
              nsLayoutUtils::
                  GetTransformToAncestorScaleCrossProcessForFrameMetrics(frame);

          float scale = std::max(cumulativeResolution.xScale,
                                 cumulativeResolution.yScale);

          fontRange->mFontSize = frame->PresContext()->CSSPixelsToDevPixels(
              font.size.ToCSSPixels() * scale);
        }
      }
      baseOffset += kBRLength;
    }
  }

  aLength = baseOffset;
  return NS_OK;
}

nsresult ContentEventHandler::ExpandToClusterBoundary(
    Text& aTextNode, bool aForward, uint32_t* aXPOffset) const {
  // XXX This method assumes that the frame boundaries must be cluster
  // boundaries. It's false, but no problem now, maybe.
  if (*aXPOffset == 0 || *aXPOffset == aTextNode.TextLength()) {
    return NS_OK;
  }

  NS_ASSERTION(*aXPOffset <= aTextNode.TextLength(), "offset is out of range.");

  MOZ_DIAGNOSTIC_ASSERT(mDocument->GetPresShell());
  CaretAssociationHint hint =
      aForward ? CaretAssociationHint::Before : CaretAssociationHint::After;
  FrameAndOffset frameAndOffset = SelectionMovementUtils::GetFrameForNodeOffset(
      &aTextNode, int32_t(*aXPOffset), hint);
  if (frameAndOffset) {
    auto [startOffset, endOffset] = frameAndOffset->GetOffsets();
    if (*aXPOffset == static_cast<uint32_t>(startOffset) ||
        *aXPOffset == static_cast<uint32_t>(endOffset)) {
      return NS_OK;
    }
    if (!frameAndOffset->IsTextFrame()) {
      return NS_ERROR_FAILURE;
    }
    nsTextFrame* textFrame = static_cast<nsTextFrame*>(frameAndOffset.mFrame);
    int32_t newOffsetInFrame = *aXPOffset - startOffset;
    newOffsetInFrame += aForward ? -1 : 1;
    // PeekOffsetCharacter() should respect cluster but ignore user-select
    // style.  If it returns "FOUND", we should use the result.  Otherwise,
    // we shouldn't use the result because the offset was moved to reversed
    // direction.
    nsTextFrame::PeekOffsetCharacterOptions options;
    options.mRespectClusters = true;
    options.mIgnoreUserStyleAll = true;
    if (textFrame->PeekOffsetCharacter(aForward, &newOffsetInFrame, options) ==
        nsIFrame::FOUND) {
      *aXPOffset = startOffset + newOffsetInFrame;
      return NS_OK;
    }
  }

  // If the frame isn't available, we only can check surrogate pair...
  if (aTextNode.DataBuffer().IsLowSurrogateFollowingHighSurrogateAt(
          *aXPOffset)) {
    *aXPOffset += aForward ? 1 : -1;
  }
  return NS_OK;
}

already_AddRefed<dom::Range> ContentEventHandler::GetRangeFromFlatTextOffset(
    WidgetContentCommandEvent* aEvent, uint32_t aOffset, uint32_t aLength) {
  nsresult rv = InitCommon(aEvent->mMessage);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  Result<DOMRangeAndAdjustedOffsetInFlattenedText, nsresult> result =
      ConvertFlatTextOffsetToDOMRange(aOffset, aLength, false);
  if (NS_WARN_IF(result.isErr())) {
    return nullptr;
  }

  DOMRangeAndAdjustedOffsetInFlattenedText domRangeAndAdjustOffset =
      result.unwrap();

  return dom::Range::Create(domRangeAndAdjustOffset.mRange.Start(),
                            domRangeAndAdjustOffset.mRange.End(),
                            IgnoreErrors());
}

template <typename RangeType, typename TextNodeType>
Result<ContentEventHandler::DOMRangeAndAdjustedOffsetInFlattenedTextBase<
           RangeType, TextNodeType>,
       nsresult>
ContentEventHandler::ConvertFlatTextOffsetToDOMRangeBase(
    uint32_t aOffset, uint32_t aLength, bool aExpandToClusterBoundaries) {
  DOMRangeAndAdjustedOffsetInFlattenedTextBase<RangeType, TextNodeType> result;
  result.mAdjustedOffset = aOffset;

  // Special case like <br contenteditable>
  if (!mRootElement->HasChildren()) {
    nsresult rv = result.mRange.CollapseTo(
        RawRangeBoundary::StartOfParent(*mRootElement));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
  }

  UnsafePreContentIterator preOrderIter;
  nsresult rv = preOrderIter.Init(mRootElement);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Err(rv);
  }

  uint32_t offset = 0;
  uint32_t endOffset = aOffset + aLength;
  bool startSet = false;
  for (; !preOrderIter.IsDone(); preOrderIter.Next()) {
    nsINode* node = preOrderIter.GetCurrentNode();
    if (NS_WARN_IF(!node)) {
      break;
    }
    // FYI: mRootElement shouldn't cause any text. So, we can skip it simply.
    if (node == mRootElement || !node->IsContent()) {
      continue;
    }
    nsIContent* const content = node->AsContent();
    Text* const contentAsText = Text::FromNode(content);

    if (contentAsText) {
      result.mLastTextNode = contentAsText;
    }

    uint32_t textLength =
        contentAsText
            ? GetTextLength(*contentAsText)
            : (ShouldBreakLineBefore(*content, mRootElement) ? kBRLength : 0);
    if (!textLength) {
      continue;
    }

    // When the start offset is in between accumulated offset and the last
    // offset of the node, the node is the start node of the range.
    if (!startSet && aOffset <= offset + textLength) {
      nsINode* startNode = nullptr;
      Maybe<uint32_t> startNodeOffset;
      if (contentAsText) {
        // Rule #1.1: [textNode or text[Node or textNode[
        uint32_t xpOffset = aOffset - offset;
        if (aExpandToClusterBoundaries) {
          const uint32_t oldXPOffset = xpOffset;
          nsresult rv =
              ExpandToClusterBoundary(*contentAsText, false, &xpOffset);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return Err(rv);
          }
          // This is correct since a cluster shouldn't include line break.
          result.mAdjustedOffset -= (oldXPOffset - xpOffset);
        }
        startNode = contentAsText;
        startNodeOffset = Some(xpOffset);
      } else if (aOffset < offset + textLength) {
        // Rule #1.2 [<element>
        startNode = content->GetParent();
        if (NS_WARN_IF(!startNode)) {
          return Err(NS_ERROR_FAILURE);
        }
        startNodeOffset = startNode->ComputeIndexOf(content);
        if (NS_WARN_IF(startNodeOffset.isNothing())) {
          // The content is being removed from the parent!
          return Err(NS_ERROR_FAILURE);
        }
      } else if (!content->HasChildren()) {
        // Rule #1.3: <element/>[
        startNode = content->GetParent();
        if (NS_WARN_IF(!startNode)) {
          return Err(NS_ERROR_FAILURE);
        }
        startNodeOffset = startNode->ComputeIndexOf(content);
        if (NS_WARN_IF(startNodeOffset.isNothing())) {
          // The content is being removed from the parent!
          return Err(NS_ERROR_FAILURE);
        }
        MOZ_ASSERT(*startNodeOffset != UINT32_MAX);
        ++(*startNodeOffset);
      } else {
        // Rule #1.4: <element>[
        startNode = content;
        startNodeOffset = Some(0);
      }
      NS_ASSERTION(startNode, "startNode must not be nullptr");
      MOZ_ASSERT(startNodeOffset.isSome(),
                 "startNodeOffset must not be Nothing");
      rv = result.mRange.SetStart(startNode, *startNodeOffset);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return Err(rv);
      }
      startSet = true;

      if (!aLength) {
        rv = result.mRange.SetEnd(startNode, *startNodeOffset);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return Err(rv);
        }
        return result;
      }
    }

    // When the end offset is in the content, the node is the end node of the
    // range.
    if (endOffset <= offset + textLength) {
      MOZ_ASSERT(startSet, "The start of the range should've been set already");
      if (contentAsText) {
        // Rule #2.1: ]textNode or text]Node or textNode]
        uint32_t xpOffset = endOffset - offset;
        if (aExpandToClusterBoundaries) {
          nsresult rv =
              ExpandToClusterBoundary(*contentAsText, true, &xpOffset);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return Err(rv);
          }
        }
        NS_ASSERTION(xpOffset <= INT32_MAX, "The end node offset is too large");
        nsresult rv = result.mRange.SetEnd(contentAsText, xpOffset);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return Err(rv);
        }
        return result;
      }

      if (endOffset == offset) {
        // Rule #2.2: ]<element>
        // NOTE: Please don't crash on release builds because it must be
        //       overreaction but we shouldn't allow this bug when some
        //       automated tests find this.
        MOZ_ASSERT(false,
                   "This case should've already been handled at "
                   "the last node which caused some text");
        return Err(NS_ERROR_FAILURE);
      }

      if (content->HasChildren() &&
          ShouldBreakLineBefore(*content, mRootElement)) {
        // Rule #2.3: </element>]
        rv = result.mRange.SetEnd(content, 0);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return Err(rv);
        }
        return result;
      }

      // Rule #2.4: <element/>]
      nsINode* endNode = content->GetParent();
      if (NS_WARN_IF(!endNode)) {
        return Err(NS_ERROR_FAILURE);
      }
      const Maybe<uint32_t> indexInParent = endNode->ComputeIndexOf(content);
      if (NS_WARN_IF(indexInParent.isNothing())) {
        // The content is being removed from the parent!
        return Err(NS_ERROR_FAILURE);
      }
      MOZ_ASSERT(*indexInParent != UINT32_MAX);
      rv = result.mRange.SetEnd(endNode, *indexInParent + 1);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return Err(rv);
      }
      return result;
    }

    offset += textLength;
  }

  if (!startSet) {
    if (!offset) {
      // Rule #1.5: <root>[</root>
      // When there are no nodes causing text, the start of the DOM range
      // should be start of the root node since clicking on such editor (e.g.,
      // <div contenteditable><span></span></div>) sets caret to the start of
      // the editor (i.e., before <span> in the example).
      rv = result.mRange.SetStart(mRootElement, 0);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return Err(rv);
      }
      if (!aLength) {
        rv = result.mRange.SetEnd(mRootElement, 0);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return Err(rv);
        }
        return result;
      }
    } else {
      // Rule #1.5: [</root>
      rv = result.mRange.SetStart(mRootElement, mRootElement->GetChildCount());
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return result;
      }
    }
    result.mAdjustedOffset = offset;
  }
  // Rule #2.5: ]</root>
  rv = result.mRange.SetEnd(mRootElement, mRootElement->GetChildCount());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Err(rv);
  }
  return result;
}

nsresult ContentEventHandler::HandleQueryContentEvent(
    WidgetQueryContentEvent* aEvent) {
  nsresult rv = NS_ERROR_NOT_IMPLEMENTED;
  switch (aEvent->mMessage) {
    case eQuerySelectedText:
      rv = OnQuerySelectedText(aEvent);
      break;
    case eQueryTextContent:
      rv = OnQueryTextContent(aEvent);
      break;
    case eQueryCaretRect:
      rv = OnQueryCaretRect(aEvent);
      break;
    case eQueryTextRect:
      rv = OnQueryTextRect(aEvent);
      break;
    case eQueryTextRectArray:
      rv = OnQueryTextRectArray(aEvent);
      break;
    case eQueryEditorRect:
      rv = OnQueryEditorRect(aEvent);
      break;
    case eQueryContentState:
      rv = OnQueryContentState(aEvent);
      break;
    case eQuerySelectionAsTransferable:
      rv = OnQuerySelectionAsTransferable(aEvent);
      break;
    case eQueryCharacterAtPoint:
      rv = OnQueryCharacterAtPoint(aEvent);
      break;
    case eQueryDOMWidgetHittest:
      rv = OnQueryDOMWidgetHittest(aEvent);
      break;
    case eQueryDropTargetHittest:
      rv = OnQueryDropTargetHittest(aEvent);
      break;
    default:
      break;
  }
  if (NS_FAILED(rv)) {
    aEvent->mReply.reset();  // Mark the query failed.
    return rv;
  }

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

// Similar to nsFrameSelection::GetFrameForNodeOffset,
// but this is more flexible for OnQueryTextRect to use
static Result<nsIFrame*, nsresult> GetFrameForTextRect(const nsINode* aNode,
                                                       int32_t aNodeOffset,
                                                       bool aHint) {
  const nsIContent* content = nsIContent::FromNodeOrNull(aNode);
  if (NS_WARN_IF(!content)) {
    return Err(NS_ERROR_UNEXPECTED);
  }
  nsIFrame* frame = content->GetPrimaryFrame();
  // The node may be invisible, e.g., `display: none`, invisible text node
  // around block elements, etc.  Therefore, don't warn when we don't find
  // a primary frame.
  if (!frame) {
    return nullptr;
  }
  int32_t childNodeOffset = 0;
  nsIFrame* returnFrame = nullptr;
  nsresult rv = frame->GetChildFrameContainingOffset(
      aNodeOffset, aHint, &childNodeOffset, &returnFrame);
  if (NS_FAILED(rv)) {
    return Err(rv);
  }
  return returnFrame;
}

nsresult ContentEventHandler::OnQuerySelectedText(
    WidgetQueryContentEvent* aEvent) {
  nsresult rv = Init(aEvent);
  if (NS_FAILED(rv)) {
    return rv;
  }

  MOZ_ASSERT(aEvent->mReply->mOffsetAndData.isNothing());

  if (RefPtr<EditContext> editContext = GetEditContext()) {
    // Get text directly from EditContext, instead of trying to get it from
    // the DOM.
    uint32_t selectionStart;
    uint32_t selectionEnd;
    if (aEvent->mInput.mSelectionType == SelectionType::eNormal) {
      selectionStart = editContext->SelectionStartClamped();
      selectionEnd = editContext->SelectionEndClamped();
    } else {
      selectionStart = mSelection->AnchorOffset();
      selectionEnd = mSelection->FocusOffset();
    }
    uint32_t selectionMin = std::min(selectionStart, selectionEnd);
    uint32_t selectionMax = std::max(selectionStart, selectionEnd);
    nsAutoString selectedText;
    editContext->GetTextSubstring(selectionMin, selectionMax, selectedText);
    aEvent->mReply->mOffsetAndData.emplace(selectionMin, selectedText,
                                           OffsetAndDataFor::SelectedString);
    aEvent->mReply->mWritingMode = editContext->WritingMode();
    aEvent->mReply->mReversed = selectionEnd < selectionStart;
    return NS_OK;
  }

  if (!mFirstSelectedSimpleRange.IsPositioned()) {
    MOZ_ASSERT(aEvent->mReply->mOffsetAndData.isNothing());
    // This is special case that `mReply` is emplaced, but mOffsetAndData is
    // not emplaced but treated as succeeded because of no selection ranges
    // is a usual case.
    return NS_OK;
  }

  const UnsafeSimpleRange firstSelectedSimpleRange(mFirstSelectedSimpleRange);
  nsINode* const startNode = firstSelectedSimpleRange.GetStartContainer();
  nsINode* const endNode = firstSelectedSimpleRange.GetEndContainer();

  // Make sure the selection is within the root content range.
  if (!startNode->IsInclusiveDescendantOf(mRootElement) ||
      !endNode->IsInclusiveDescendantOf(mRootElement)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  const Result<uint32_t, nsresult> startOffsetOrError =
      GetStartOffset(firstSelectedSimpleRange);
  if (NS_WARN_IF(startOffsetOrError.isErr())) {
    return NS_ERROR_FAILURE;
  }

  const RawRangeBoundary anchorRef = mSelection->RangeCount() > 0
                                         ? mSelection->AnchorRef().AsRaw()
                                         : firstSelectedSimpleRange.Start();
  const RawRangeBoundary focusRef = mSelection->RangeCount() > 0
                                        ? mSelection->FocusRef().AsRaw()
                                        : firstSelectedSimpleRange.End();
  if (NS_WARN_IF(!anchorRef.IsSet()) || NS_WARN_IF(!focusRef.IsSet())) {
    return NS_ERROR_FAILURE;
  }

  if (mSelection->RangeCount()) {
    // If there is only one selection range, the anchor/focus node and offset
    // are the information of the range.  Therefore, we have the direction
    // information.
    if (mSelection->RangeCount() == 1) {
      // The selection's points should always be comparable, independent of the
      // selection (see nsISelectionController.idl).
      Maybe<int32_t> compare =
          nsContentUtils::ComparePoints<TreeKind::ShadowIncludingDOM>(anchorRef,
                                                                      focusRef);
      if (compare.isNothing()) {
        return NS_ERROR_FAILURE;
      }

      aEvent->mReply->mReversed = compare.value() > 0;
    }
    // However, if there are 2 or more selection ranges, we have no information
    // of that.
    else {
      aEvent->mReply->mReversed = false;
    }

    nsString selectedString;
    if (!firstSelectedSimpleRange.Collapsed() &&
        NS_WARN_IF(NS_FAILED(GenerateFlatTextContent(firstSelectedSimpleRange,
                                                     selectedString)))) {
      return NS_ERROR_FAILURE;
    }
    aEvent->mReply->mOffsetAndData.emplace(startOffsetOrError.inspect(),
                                           selectedString,
                                           OffsetAndDataFor::SelectedString);
  } else {
    NS_ASSERTION(anchorRef == focusRef,
                 "When mSelection doesn't have selection, "
                 "mFirstSelectedRawRange must be collapsed");

    aEvent->mReply->mReversed = false;
    aEvent->mReply->mOffsetAndData.emplace(startOffsetOrError.inspect(),
                                           EmptyString(),
                                           OffsetAndDataFor::SelectedString);
  }

  Result<nsIFrame*, nsresult> frameForTextRectOrError = GetFrameForTextRect(
      focusRef.GetContainer(),
      focusRef.Offset(RawRangeBoundary::OffsetFilter::kValidOffsets).valueOr(0),
      true);
  if (NS_WARN_IF(frameForTextRectOrError.isErr()) ||
      !frameForTextRectOrError.inspect()) {
    aEvent->mReply->mWritingMode = WritingMode();
  } else {
    aEvent->mReply->mWritingMode =
        frameForTextRectOrError.inspect()->GetWritingMode();
  }

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::OnQueryTextContent(
    WidgetQueryContentEvent* aEvent) {
  nsresult rv = Init(aEvent);
  if (NS_FAILED(rv)) {
    return rv;
  }

  MOZ_ASSERT(aEvent->mReply->mOffsetAndData.isNothing());

  Result<UnsafeDOMRangeAndAdjustedOffsetInFlattenedText, nsresult>
      domRangeAndAdjustedOffsetOrError = ConvertFlatTextOffsetToUnsafeDOMRange(
          aEvent->mInput.mOffset, aEvent->mInput.mLength, false);

  if (EditContext* editContext = GetEditContext()) {
    // Get text directly from EditContext, instead of trying to get it
    // from the DOM.
    nsAutoString text;
    uint32_t start = aEvent->mInput.mOffset;
    uint32_t end = aEvent->mInput.EndOffset();
    editContext->GetTextSubstring(start, end, text);
    aEvent->mReply->mOffsetAndData.emplace(start, text);
    // Get mFontRanges from the DOM. For canvas-based EditContext we don't know
    // what the font is.
    if (!mRootElement->IsHTMLElement(nsGkAtoms::canvas) &&
        domRangeAndAdjustedOffsetOrError.isOk()) {
      uint32_t fontRangeLength;
      GenerateFlatFontRanges(domRangeAndAdjustedOffsetOrError.unwrap().mRange,
                             aEvent->mReply->mFontRanges, fontRangeLength);
    }
    return NS_OK;
  }

  if (MOZ_UNLIKELY(domRangeAndAdjustedOffsetOrError.isErr())) {
    NS_WARNING(
        "ContentEventHandler::ConvertFlatTextOffsetToDOMRangeBase() failed");
    return NS_ERROR_FAILURE;
  }
  const UnsafeDOMRangeAndAdjustedOffsetInFlattenedText
      domRangeAndAdjustedOffset = domRangeAndAdjustedOffsetOrError.unwrap();

  nsString textInRange;
  if (NS_WARN_IF(NS_FAILED(GenerateFlatTextContent(
          domRangeAndAdjustedOffset.mRange, textInRange)))) {
    return NS_ERROR_FAILURE;
  }

  aEvent->mReply->mOffsetAndData.emplace(
      domRangeAndAdjustedOffset.mAdjustedOffset, textInRange,
      OffsetAndDataFor::EditorString);

  if (aEvent->mWithFontRanges) {
    uint32_t fontRangeLength;
    if (NS_WARN_IF(NS_FAILED(GenerateFlatFontRanges(
            domRangeAndAdjustedOffset.mRange, aEvent->mReply->mFontRanges,
            fontRangeLength)))) {
      return NS_ERROR_FAILURE;
    }

    MOZ_ASSERT(fontRangeLength == aEvent->mReply->DataLength(),
               "Font ranges doesn't match the string");
  }

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

void ContentEventHandler::EnsureNonEmptyRect(nsRect& aRect) const {
  // See the comment in ContentEventHandler.h why this doesn't set them to
  // one device pixel.
  aRect.height = std::max(1, aRect.height);
  aRect.width = std::max(1, aRect.width);
}

void ContentEventHandler::EnsureNonEmptyRect(LayoutDeviceIntRect& aRect) const {
  aRect.height = std::max(1, aRect.height);
  aRect.width = std::max(1, aRect.width);
}

template <typename NodeType, typename RangeBoundaryType>
ContentEventHandler::FrameAndNodeOffset
ContentEventHandler::GetFirstFrameInRangeForTextRect(
    const SimpleRangeBase<NodeType, RangeBoundaryType>& aSimpleRange) {
  RawNodePosition nodePosition;
  UnsafePreContentIterator preOrderIter;
  nsresult rv = preOrderIter.Init(aSimpleRange.Start().AsRaw(),
                                  aSimpleRange.End().AsRaw());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return FrameAndNodeOffset();
  }
  for (; !preOrderIter.IsDone(); preOrderIter.Next()) {
    nsINode* node = preOrderIter.GetCurrentNode();
    if (NS_WARN_IF(!node)) {
      break;
    }

    auto* content = nsIContent::FromNode(node);
    if (MOZ_UNLIKELY(!content)) {
      continue;
    }

    // If the node is invisible (e.g., the node is or is in an invisible node or
    // it's a white-space only text node around a block boundary), we should
    // ignore it.
    if (!content->GetPrimaryFrame()) {
      continue;
    }

    if (auto* textNode = Text::FromNode(content)) {
      // If the range starts at the end of a text node, we need to find
      // next node which causes text.
      const uint32_t offsetInNode = textNode == aSimpleRange.GetStartContainer()
                                        ? aSimpleRange.StartOffset()
                                        : 0u;
      if (offsetInNode < textNode->TextDataLength()) {
        nodePosition = {textNode, offsetInNode};
        break;
      }
      continue;
    }

    // If the element node causes a line break before it, it's the first
    // node causing text.
    if (ShouldBreakLineBefore(*content, mRootElement) ||
        IsPaddingBR(*content)) {
      nodePosition = {content, 0u};
    }
  }

  if (!nodePosition.IsSetAndValid()) {
    return FrameAndNodeOffset();
  }

  Result<nsIFrame*, nsresult> firstFrameOrError = GetFrameForTextRect(
      nodePosition.GetContainer(),
      *nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets), true);
  if (NS_WARN_IF(firstFrameOrError.isErr()) || !firstFrameOrError.inspect()) {
    return FrameAndNodeOffset();
  }
  return FrameAndNodeOffset(
      firstFrameOrError.inspect(),
      *nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets));
}

template <typename NodeType, typename RangeBoundaryType>
ContentEventHandler::FrameAndNodeOffset
ContentEventHandler::GetLastFrameInRangeForTextRect(
    const SimpleRangeBase<NodeType, RangeBoundaryType>& aSimpleRange) {
  RawNodePosition nodePosition;
  UnsafePreContentIterator preOrderIter;
  nsresult rv = preOrderIter.Init(aSimpleRange.Start().AsRaw(),
                                  aSimpleRange.End().AsRaw());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return FrameAndNodeOffset();
  }

  const RangeBoundaryType& endPoint = aSimpleRange.End();
  MOZ_ASSERT(endPoint.IsSetAndValid());
  // If the end point is start of a text node or specified by its parent and
  // index, the node shouldn't be included into the range.  For example,
  // with this case, |<p>abc[<br>]def</p>|, the range ends at 3rd children of
  // <p> (see the range creation rules, "2.4. Cases: <element/>]"). This causes
  // following frames:
  // +----+-----+
  // | abc|[<br>|
  // +----+-----+
  // +----+
  // |]def|
  // +----+
  // So, if this method includes the 2nd text frame's rect to its result, the
  // caller will return too tall rect which includes 2 lines in this case isn't
  // expected by native IME  (e.g., popup of IME will be positioned at bottom
  // of "d" instead of right-bottom of "c").  Therefore, this method shouldn't
  // include the last frame when its content isn't really in aSimpleRange.
  nsINode* nextNodeOfRangeEnd = nullptr;
  if (endPoint.GetContainer()->IsText()) {
    // Don't set nextNodeOfRangeEnd to the start node of aSimpleRange because if
    // the container of the end is same as start node of the range, the text
    // node shouldn't be next of range end even if the offset is 0.  This
    // could occur with empty text node.
    if (endPoint.IsStartOfContainer() &&
        aSimpleRange.GetStartContainer() != endPoint.GetContainer()) {
      nextNodeOfRangeEnd = endPoint.GetContainer();
    }
  } else if (endPoint.IsSetAndValid()) {
    nextNodeOfRangeEnd = endPoint.GetChildAtOffset();
  }

  for (preOrderIter.Last(); !preOrderIter.IsDone(); preOrderIter.Prev()) {
    nsINode* node = preOrderIter.GetCurrentNode();
    if (NS_WARN_IF(!node)) {
      break;
    }

    if (node == nextNodeOfRangeEnd) {
      continue;
    }

    auto* content = nsIContent::FromNode(node);
    if (MOZ_UNLIKELY(!content)) {
      continue;
    }

    // If the node is invisible (e.g., the node is or is in an invisible node or
    // it's a white-space only text node around a block boundary), we should
    // ignore it.
    if (!content->GetPrimaryFrame()) {
      continue;
    }

    if (auto* textNode = Text::FromNode(node)) {
      nodePosition = {textNode, textNode == aSimpleRange.GetEndContainer()
                                    ? aSimpleRange.EndOffset()
                                    : textNode->TextDataLength()};

      // If the text node is empty or the last node of the range but the index
      // is 0, we should store current position but continue looking for
      // previous node (If there are no nodes before it, we should use current
      // node position for returning its frame).
      if (*nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets) ==
          0) {
        continue;
      }
      break;
    }

    if (ShouldBreakLineBefore(*content, mRootElement) ||
        IsPaddingBR(*content)) {
      nodePosition = {content, 0u};
      break;
    }
  }

  if (!nodePosition.IsSet()) {
    return FrameAndNodeOffset();
  }

  Result<nsIFrame*, nsresult> lastFrameOrError = GetFrameForTextRect(
      nodePosition.GetContainer(),
      *nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets), true);
  if (NS_WARN_IF(lastFrameOrError.isErr()) || !lastFrameOrError.inspect()) {
    return FrameAndNodeOffset();
  }

  // If the last frame is a text frame, we need to check if the range actually
  // includes at least one character in the range.  Therefore, if it's not a
  // text frame, we need to do nothing anymore.
  if (!lastFrameOrError.inspect()->IsTextFrame()) {
    return FrameAndNodeOffset(
        lastFrameOrError.inspect(),
        *nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets));
  }

  int32_t start = lastFrameOrError.inspect()->GetOffsets().first;

  // If the start offset in the node is same as the computed offset in the
  // node and it's not 0, the frame shouldn't be added to the text rect.  So,
  // this should return previous text frame and its last offset if there is
  // at least one text frame.
  if (*nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets) &&
      *nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets) ==
          static_cast<uint32_t>(start)) {
    const uint32_t newNodePositionOffset =
        *nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets);
    MOZ_ASSERT(newNodePositionOffset != 0);
    nodePosition = {nodePosition.GetContainer(), newNodePositionOffset - 1u};
    lastFrameOrError = GetFrameForTextRect(
        nodePosition.GetContainer(),
        *nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets),
        true);
    if (NS_WARN_IF(lastFrameOrError.isErr()) || !lastFrameOrError.inspect()) {
      return FrameAndNodeOffset();
    }
  }

  return FrameAndNodeOffset(
      lastFrameOrError.inspect(),
      *nodePosition.Offset(RawNodePosition::OffsetFilter::kValidOffsets));
}

ContentEventHandler::FrameRelativeRect
ContentEventHandler::GetLineBreakerRectBefore(nsIFrame* aFrame) {
  // Note that this method should be called only with an element's frame whose
  // open tag causes a line break or moz-<br> for computing empty last line's
  // rect.
  MOZ_ASSERT(aFrame->GetContent());
  MOZ_ASSERT(ShouldBreakLineBefore(*aFrame->GetContent(), mRootElement) ||
             IsPaddingBR(*aFrame->GetContent()));

  nsIFrame* frameForFontMetrics = aFrame;

  // If it's not a <br> frame, this method computes the line breaker's rect
  // outside the frame.  Therefore, we need to compute with parent frame's
  // font metrics in such case.
  if (!aFrame->IsBrFrame() && aFrame->GetParent()) {
    frameForFontMetrics = aFrame->GetParent();
  }

  // Note that <br> element's rect is decided with line-height but we need
  // a rect only with font height.  Additionally, <br> frame's width and
  // height are 0 in quirks mode if it's not an empty line.  So, we cannot
  // use frame rect information even if it's a <br> frame.

  RefPtr<nsFontMetrics> fontMetrics =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(frameForFontMetrics);
  if (NS_WARN_IF(!fontMetrics)) {
    return FrameRelativeRect();
  }

  const WritingMode kWritingMode = frameForFontMetrics->GetWritingMode();

  auto caretBlockAxisMetrics =
      aFrame->GetCaretBlockAxisMetrics(kWritingMode, *fontMetrics);
  nscoord inlineOffset = 0;

  // If aFrame isn't a <br> frame, caret should be at outside of it because
  // the line break is before its open tag.  For example, case of
  // |<div><p>some text</p></div>|, caret is before <p> element and in <div>
  // element, the caret should be left of top-left corner of <p> element like:
  //
  // +-<div>-------------------  <div>'s border box
  // | I +-<p>-----------------  <p>'s border box
  // | I |
  // | I |
  // |   |
  //   ^- caret
  //
  // However, this is a hack for unusual scenario.  This hack shouldn't be
  // used as far as possible.
  if (!aFrame->IsBrFrame()) {
    if (kWritingMode.IsVertical() && !kWritingMode.IsLineInverted()) {
      // above of top-right corner of aFrame.
      caretBlockAxisMetrics.mOffset =
          aFrame->GetRect().XMost() - caretBlockAxisMetrics.mExtent;
    } else {
      // above (For vertical) or left (For horizontal) of top-left corner of
      // aFrame.
      caretBlockAxisMetrics.mOffset = 0;
    }
    inlineOffset = -aFrame->PresContext()->AppUnitsPerDevPixel();
  }
  FrameRelativeRect result(aFrame);
  if (kWritingMode.IsVertical()) {
    result.mRect.x = caretBlockAxisMetrics.mOffset;
    result.mRect.y = inlineOffset;
    result.mRect.width = caretBlockAxisMetrics.mExtent;
  } else {
    result.mRect.x = inlineOffset;
    result.mRect.y = caretBlockAxisMetrics.mOffset;
    result.mRect.height = caretBlockAxisMetrics.mExtent;
  }
  return result;
}

ContentEventHandler::FrameRelativeRect
ContentEventHandler::GuessLineBreakerRectAfter(const Text& aTextNode) {
  FrameRelativeRect result;
  const int32_t length = static_cast<int32_t>(aTextNode.TextLength());
  if (NS_WARN_IF(length < 0)) {
    return result;
  }
  // Get the last nsTextFrame which is caused by aTextNode.  Note that
  // a text node can cause multiple text frames, e.g., the text is too long
  // and wrapped by its parent block or the text has line breakers and its
  // white-space property respects the line breakers (e.g., |pre|).
  Result<nsIFrame*, nsresult> lastTextFrameOrError =
      GetFrameForTextRect(&aTextNode, length, true);
  if (NS_WARN_IF(lastTextFrameOrError.isErr()) ||
      !lastTextFrameOrError.inspect()) {
    return result;
  }
  const nsRect kLastTextFrameRect = lastTextFrameOrError.inspect()->GetRect();
  if (lastTextFrameOrError.inspect()->GetWritingMode().IsVertical()) {
    // Below of the last text frame.
    result.mRect.SetRect(0, kLastTextFrameRect.height, kLastTextFrameRect.width,
                         0);
  } else {
    // Right of the last text frame (not bidi-aware).
    result.mRect.SetRect(kLastTextFrameRect.width, 0, 0,
                         kLastTextFrameRect.height);
  }
  result.mBaseFrame = lastTextFrameOrError.unwrap();
  return result;
}

ContentEventHandler::FrameRelativeRect
ContentEventHandler::GuessFirstCaretRectIn(nsIFrame* aFrame) {
  const WritingMode kWritingMode = aFrame->GetWritingMode();
  nsPresContext* presContext = aFrame->PresContext();

  // Computes the font height, but if it's not available, we should use
  // default font size of Firefox.  The default font size in default settings
  // is 16px.
  RefPtr<nsFontMetrics> fontMetrics =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(aFrame);
  const nscoord kMaxHeight = fontMetrics
                                 ? fontMetrics->MaxHeight()
                                 : 16 * presContext->AppUnitsPerDevPixel();

  nsRect caretRect;
  const nsRect kContentRect = aFrame->GetContentRect() - aFrame->GetPosition();
  caretRect.y = kContentRect.y;
  if (!kWritingMode.IsVertical()) {
    if (kWritingMode.IsBidiLTR()) {
      caretRect.x = kContentRect.x;
    } else {
      // Move 1px left for the space of caret itself.
      const nscoord kOnePixel = presContext->AppUnitsPerDevPixel();
      caretRect.x = kContentRect.XMost() - kOnePixel;
    }
    caretRect.height = kMaxHeight;
    // However, don't add kOnePixel here because it may cause 2px width at
    // aligning the edge to device pixels.
    caretRect.width = 1;
  } else {
    if (kWritingMode.IsVerticalLR()) {
      caretRect.x = kContentRect.x;
    } else {
      caretRect.x = kContentRect.XMost() - kMaxHeight;
    }
    caretRect.width = kMaxHeight;
    // Don't add app units for a device pixel because it may cause 2px height
    // at aligning the edge to device pixels.
    caretRect.height = 1;
  }
  return FrameRelativeRect(caretRect, aFrame);
}

//  static
LayoutDeviceIntRect ContentEventHandler::GetCaretRectBefore(
    const LayoutDeviceIntRect& aCharRect, const WritingMode& aWritingMode) {
  LayoutDeviceIntRect caretRectBefore(aCharRect);
  if (aWritingMode.IsVertical()) {
    caretRectBefore.height = 1;
  } else {
    // TODO: Make here bidi-aware.
    caretRectBefore.width = 1;
  }
  return caretRectBefore;
}

//  static
nsRect ContentEventHandler::GetCaretRectBefore(
    const nsRect& aCharRect, const WritingMode& aWritingMode) {
  nsRect caretRectBefore(aCharRect);
  if (aWritingMode.IsVertical()) {
    // For making the height 1 device pixel after aligning the rect edges to
    // device pixels, don't add one device pixel in app units here.
    caretRectBefore.height = 1;
  } else {
    // TODO: Make here bidi-aware.
    // For making the width 1 device pixel after aligning the rect edges to
    // device pixels, don't add one device pixel in app units here.
    caretRectBefore.width = 1;
  }
  return caretRectBefore;
}

//  static
LayoutDeviceIntRect ContentEventHandler::GetCaretRectAfter(
    const LayoutDeviceIntRect& aCharRect, const WritingMode& aWritingMode) {
  LayoutDeviceIntRect caretRectAfter(aCharRect);
  if (aWritingMode.IsVertical()) {
    caretRectAfter.y = aCharRect.YMost() + 1;
    caretRectAfter.height = 1;
  } else {
    // TODO: Make here bidi-aware.
    caretRectAfter.x = aCharRect.XMost() + 1;
    caretRectAfter.width = 1;
  }
  return caretRectAfter;
}

//  static
nsRect ContentEventHandler::GetCaretRectAfter(nsPresContext& aPresContext,
                                              const nsRect& aCharRect,
                                              const WritingMode& aWritingMode) {
  nsRect caretRectAfter(aCharRect);
  const nscoord onePixel = aPresContext.AppUnitsPerDevPixel();
  if (aWritingMode.IsVertical()) {
    caretRectAfter.y = aCharRect.YMost() + onePixel;
    // For making the height 1 device pixel after aligning the rect edges to
    // device pixels, don't add one device pixel in app units here.
    caretRectAfter.height = 1;
  } else {
    // TODO: Make here bidi-aware.
    caretRectAfter.x = aCharRect.XMost() + onePixel;
    // For making the width 1 device pixel after aligning the rect edges to
    // device pixels, don't add one device pixel in app units here.
    caretRectAfter.width = 1;
  }
  return caretRectAfter;
}

nsresult ContentEventHandler::OnQueryTextRectArray(
    WidgetQueryContentEvent* aEvent) {
  nsresult rv = Init(aEvent);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  MOZ_ASSERT(aEvent->mReply->mOffsetAndData.isNothing());

  WritingMode lastVisibleFrameWritingMode;
  LayoutDeviceIntRect rect;
  uint32_t offset = aEvent->mInput.mOffset;
  const uint32_t kEndOffset = aEvent->mInput.EndOffset();
  bool wasLineBreaker = false;
  if (EditContext* editContext = GetEditContext()) {
    MOZ_ASSERT(offset <= kEndOffset);
    // Let's not overflow if somehow offset > kEndOffset
    const uint32_t endOffset = std::max(kEndOffset, offset);
    // Get rectangles from EditContext character bounds
    nsTArray<LayoutDeviceIntRect>& rects = aEvent->mReply->mRectArray;
    Maybe<LayoutDeviceIntRect> selectionBounds =
        editContext->GetSelectionBounds();
    if (selectionBounds && offset == endOffset &&
        offset == editContext->SelectionStartClamped() &&
        editContext->SelectionIsCollapsed()) {
      // Use selection bounds for caret rect
      rects.AppendElement(*selectionBounds);
      MOZ_ASSERT(aEvent->Succeeded());
      return NS_OK;
    }
    rv = editContext->GetCharacterBounds(offset, endOffset, rects);
    if (NS_SUCCEEDED(rv) && !rects.IsEmpty()) {
      LayoutDeviceIntRect lastRect = rects.LastElement();
      // If a range that goes past the end of the text content was requested,
      // just add more copies of the last rect to match the expected length.
      while (rects.Length() < endOffset - offset) {
        rects.AppendElement(lastRect);
      }
      MOZ_ASSERT(aEvent->Succeeded());
      return NS_OK;
    }
    // If failed (e.g. web app does not call updateCharacterBounds()), try
    // to get the text from the DOM instead. But for canvas, that certainly
    // won't work, so just use the fallback bounds.
    if (mRootElement->IsHTMLElement(nsGkAtoms::canvas)) {
      nsTArray<LayoutDeviceIntRect>& rects = aEvent->mReply->mRectArray;
      LayoutDeviceIntRect fallbackBounds = editContext->FallbackBounds();
      const uint32_t rectCount = std::max(1u, endOffset - offset);
      rects.SetCapacity(rectCount);
      for ([[maybe_unused]] uint32_t i : IntegerRange(rectCount)) {
        rects.AppendElement(fallbackBounds);
      }
      MOZ_ASSERT(aEvent->Succeeded());
      return NS_OK;
    }
  }
  // lastCharRect stores the last charRect value (see below for the detail of
  // charRect).
  nsRect lastCharRect;
  // lastFrame is base frame of lastCharRect.
  // TODO: We should look for this if the first text is not visible.  However,
  //       users cannot put caret invisible text and users cannot type in it
  //       at least only with user's operations.  Therefore, we don't need to
  //       fix this immediately.
  nsIFrame* lastFrame = nullptr;
  nsAutoString flattenedAllText;
  flattenedAllText.SetIsVoid(true);
  while (offset < kEndOffset) {
    Result<DOMRangeAndAdjustedOffsetInFlattenedText, nsresult>
        domRangeAndAdjustedOffsetOrError =
            ConvertFlatTextOffsetToDOMRange(offset, 1, true);
    if (MOZ_UNLIKELY(domRangeAndAdjustedOffsetOrError.isErr())) {
      NS_WARNING(
          "ContentEventHandler::ConvertFlatTextOffsetToDOMRangeBase() failed");
      return domRangeAndAdjustedOffsetOrError.unwrapErr();
    }
    const DOMRangeAndAdjustedOffsetInFlattenedText domRangeAndAdjustedOffset =
        domRangeAndAdjustedOffsetOrError.unwrap();

    // TODO: When we crossed parent block boundary now, we should fill pending
    //       character rects with caret rect after the last visible character
    //       rect.

    // If the range is collapsed, offset has already reached the end of the
    // contents.
    if (domRangeAndAdjustedOffset.mRange.Collapsed()) {
      break;
    }

    // Get the first frame which causes some text after the offset.
    FrameAndNodeOffset firstFrame =
        GetFirstFrameInRangeForTextRect(domRangeAndAdjustedOffset.mRange);

    // If GetFirstFrameInRangeForTextRect() does not return valid frame, that
    // means that the offset reached the end of contents or there is no visible
    // frame in the range generating flattened text.
    if (!firstFrame.IsValid()) {
      if (flattenedAllText.IsVoid()) {
        flattenedAllText.SetIsVoid(false);
        if (NS_WARN_IF(NS_FAILED(
                GenerateFlatTextContent(mRootElement, flattenedAllText)))) {
          NS_WARNING("ContentEventHandler::GenerateFlatTextContent() failed");
          return NS_ERROR_FAILURE;
        }
      }
      // If we've reached end of the root, append caret rect at the end of
      // the root later.
      if (offset >= flattenedAllText.Length()) {
        break;
      }
      // Otherwise, we're in an invisible node. If the node is followed by a
      // block boundary causing a line break, we can use the boundary.
      // Otherwise, if the node follows a block boundary of a parent block, we
      // can use caret rect at previous visible frame causing flattened text.
      const uint32_t remainingLengthInCurrentRange = [&]() {
        if (domRangeAndAdjustedOffset.mLastTextNode) {
          if (domRangeAndAdjustedOffset.RangeStartsFromLastTextNode()) {
            if (!domRangeAndAdjustedOffset.RangeStartsFromEndOfContainer()) {
              return domRangeAndAdjustedOffset.mLastTextNode->TextDataLength() -
                     domRangeAndAdjustedOffset.mRange.StartOffset();
            }
            return 0u;
          }
          // Must be there are not nodes which may cause generating text.
          // Therefore, we can skip all nodes before the last found text node
          // and all text in the last text node.
          return domRangeAndAdjustedOffset.mLastTextNode->TextDataLength();
        }
        if (domRangeAndAdjustedOffset.RangeStartsFromContent() &&
            ShouldBreakLineBefore(
                *domRangeAndAdjustedOffset.mRange.GetStartContainer()
                     ->AsContent(),
                mRootElement)) {
          if (kBRLength != 1u && offset - aEvent->mInput.mOffset < kBRLength) {
            // Don't return kBRLength if start position is less than the length
            // of a line-break because the offset may be between CRLF on
            // Windows.  In the case, we will be again here and gets same
            // result and we need to pay the penalty only once.  Therefore, we
            // can keep going without complicated check.
            return 1u;
          }
          return kBRLength;
        }
        return 0u;
      }();
      offset += std::max(1u, remainingLengthInCurrentRange);
      continue;
    }

    nsIContent* firstContent = firstFrame.mFrame->GetContent();
    if (NS_WARN_IF(!firstContent)) {
      return NS_ERROR_FAILURE;
    }

    bool startsBetweenLineBreaker = false;
    nsAutoString chars;
    lastVisibleFrameWritingMode = firstFrame->GetWritingMode();

    nsIFrame* baseFrame = firstFrame;
    // charRect should have each character rect or line breaker rect relative
    // to the base frame.
    AutoTArray<nsRect, 16> charRects;

    // If the first frame is a text frame, the result should be computed with
    // the frame's API.
    if (firstFrame->IsTextFrame()) {
      rv = firstFrame->GetCharacterRectsInRange(firstFrame.mOffsetInNode,
                                                kEndOffset - offset, charRects);
      if (NS_WARN_IF(NS_FAILED(rv)) || NS_WARN_IF(charRects.IsEmpty())) {
        return rv;
      }
      // Assign the characters whose rects are computed by the call of
      // nsTextFrame::GetCharacterRectsInRange().
      AppendSubString(chars, *firstContent->AsText(), firstFrame.mOffsetInNode,
                      charRects.Length());
      if (NS_WARN_IF(chars.Length() != charRects.Length())) {
        return NS_ERROR_UNEXPECTED;
      }
      if (kBRLength > 1 && chars[0] == '\n' &&
          offset == aEvent->mInput.mOffset && offset) {
        // If start of range starting from previous offset of query range is
        // same as the start of query range, the query range starts from
        // between a line breaker (i.e., the range starts between "\r" and
        // "\n").
        Result<UnsafeDOMRangeAndAdjustedOffsetInFlattenedText, nsresult>
            domRangeAndAdjustedOffsetOrError =
                ConvertFlatTextOffsetToUnsafeDOMRange(
                    aEvent->mInput.mOffset - 1, 1, true);
        if (MOZ_UNLIKELY(domRangeAndAdjustedOffsetOrError.isErr())) {
          NS_WARNING(
              "ContentEventHandler::ConvertFlatTextOffsetToDOMRangeBase() "
              "failed");
          return domRangeAndAdjustedOffsetOrError.unwrapErr();
        }
        const UnsafeDOMRangeAndAdjustedOffsetInFlattenedText
            domRangeAndAdjustedOffsetOfPreviousChar =
                domRangeAndAdjustedOffsetOrError.unwrap();
        startsBetweenLineBreaker =
            domRangeAndAdjustedOffset.mRange.GetStartContainer() ==
                domRangeAndAdjustedOffsetOfPreviousChar.mRange
                    .GetStartContainer() &&
            domRangeAndAdjustedOffset.mRange.StartOffset() ==
                domRangeAndAdjustedOffsetOfPreviousChar.mRange.StartOffset();
      }
    }
    // Other contents should cause a line breaker rect before it.
    // Note that moz-<br> element does not cause any text, however,
    // it represents empty line at the last of current block.  Therefore,
    // we need to compute its rect too.
    else if (ShouldBreakLineBefore(*firstContent, mRootElement) ||
             IsPaddingBR(*firstContent)) {
      nsRect brRect;
      // If the frame is not a <br> frame, we need to compute the caret rect
      // with last character's rect before firstContent if there is.
      // For example, if caret is after "c" of |<p>abc</p><p>def</p>|, IME may
      // query a line breaker's rect after "c".  Then, if we compute it only
      // with the 2nd <p>'s block frame, the result will be:
      //  +-<p>--------------------------------+
      //  |abc                                 |
      //  +------------------------------------+
      //
      // I+-<p>--------------------------------+
      //  |def                                 |
      //  +------------------------------------+
      // However, users expect popup windows of IME should be positioned at
      // right-bottom of "c" like this:
      //  +-<p>--------------------------------+
      //  |abcI                                |
      //  +------------------------------------+
      //
      //  +-<p>--------------------------------+
      //  |def                                 |
      //  +------------------------------------+
      // Therefore, if the first frame isn't a <br> frame and there is a text
      // node before the first node in the queried range, we should compute the
      // first rect with the previous character's rect.
      // If we already compute a character's rect in the queried range, we can
      // compute it with the cached last character's rect.  (However, don't
      // use this path if it's a <br> frame because trusting <br> frame's rect
      // is better than guessing the rect from the previous character.)
      if (!firstFrame->IsBrFrame() && !aEvent->mReply->mRectArray.IsEmpty()) {
        baseFrame = lastFrame;
        brRect = lastCharRect;
        if (!wasLineBreaker) {
          brRect = GetCaretRectAfter(*baseFrame->PresContext(), brRect,
                                     lastVisibleFrameWritingMode);
        }
      }
      // If it's not a <br> frame and it's the first character rect at the
      // queried range, we need the previous character rect of the start of
      // the queried range if there is a visible text node.
      else if (!firstFrame->IsBrFrame() &&
               domRangeAndAdjustedOffset.mLastTextNode &&
               domRangeAndAdjustedOffset.mLastTextNode->GetPrimaryFrame()) {
        FrameRelativeRect brRectRelativeToLastTextFrame =
            GuessLineBreakerRectAfter(*domRangeAndAdjustedOffset.mLastTextNode);
        if (NS_WARN_IF(!brRectRelativeToLastTextFrame.IsValid())) {
          return NS_ERROR_FAILURE;
        }
        // Look for the last text frame for the last text node.
        nsIFrame* primaryFrame =
            domRangeAndAdjustedOffset.mLastTextNode->GetPrimaryFrame();
        if (NS_WARN_IF(!primaryFrame)) {
          return NS_ERROR_FAILURE;
        }
        baseFrame = primaryFrame->LastContinuation();
        if (NS_WARN_IF(!baseFrame)) {
          return NS_ERROR_FAILURE;
        }
        brRect = brRectRelativeToLastTextFrame.RectRelativeTo(baseFrame);
      }
      // Otherwise, we need to compute the line breaker's rect only with the
      // first frame's rect.  But this may be unexpected.  For example,
      // |<div contenteditable>[<p>]abc</p></div>|.  In this case, caret is
      // before "a", therefore, users expect the rect left of "a".  However,
      // we don't have enough information about the next character here and
      // this isn't usual case (e.g., IME typically tries to query the rect
      // of "a" or caret rect for computing its popup position).  Therefore,
      // we shouldn't do more complicated hack here unless we'll get some bug
      // reports actually.
      else {
        FrameRelativeRect relativeBRRect = GetLineBreakerRectBefore(firstFrame);
        brRect = relativeBRRect.RectRelativeTo(firstFrame);
      }
      charRects.AppendElement(brRect);
      chars.AssignLiteral("\n");
      if (kBRLength > 1 && offset == aEvent->mInput.mOffset && offset) {
        // If the first frame for the previous offset of the query range and
        // the first frame for the start of query range are same, that means
        // the start offset is between the first line breaker (i.e., the range
        // starts between "\r" and "\n").
        Result<UnsafeDOMRangeAndAdjustedOffsetInFlattenedText, nsresult>
            domRangeAndAdjustedOffsetOrError =
                ConvertFlatTextOffsetToUnsafeDOMRange(
                    aEvent->mInput.mOffset - 1, 1, true);
        if (MOZ_UNLIKELY(domRangeAndAdjustedOffsetOrError.isErr())) {
          NS_WARNING(
              "ContentEventHandler::ConvertFlatTextOffsetToDOMRangeBase() "
              "failed");
          return NS_ERROR_UNEXPECTED;
        }
        const UnsafeDOMRangeAndAdjustedOffsetInFlattenedText
            domRangeAndAdjustedOffset =
                domRangeAndAdjustedOffsetOrError.unwrap();
        FrameAndNodeOffset frameForPrevious =
            GetFirstFrameInRangeForTextRect(domRangeAndAdjustedOffset.mRange);
        startsBetweenLineBreaker = frameForPrevious.mFrame == firstFrame.mFrame;
      }
    } else {
      NS_WARNING(
          "The frame is neither a text frame nor a frame whose content "
          "causes a line break");
      return NS_ERROR_FAILURE;
    }

    for (size_t i = 0; i < charRects.Length() && offset < kEndOffset; i++) {
      nsRect charRect = charRects[i];
      // Store lastCharRect before applying CSS transform because it may be
      // used for computing a line breaker rect.  Then, the computed line
      // breaker rect will be applied CSS transform again.  Therefore,
      // the value of lastCharRect should be raw rect value relative to the
      // base frame.
      lastCharRect = charRect;
      lastFrame = baseFrame;
      rv = ConvertToRootRelativeOffset(baseFrame, charRect);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      nsPresContext* presContext = baseFrame->PresContext();
      rect = LayoutDeviceIntRect::FromAppUnitsToOutside(
          charRect, presContext->AppUnitsPerDevPixel());
      if (nsPresContext* rootContext =
              presContext->GetInProcessRootContentDocumentPresContext()) {
        rect = RoundedOut(ViewportUtils::DocumentRelativeLayoutToVisual(
            rect, rootContext->PresShell()));
      }
      // Returning empty rect may cause native IME confused, let's make sure to
      // return non-empty rect.
      EnsureNonEmptyRect(rect);

      // If we found some invisible characters followed by current visible
      // character, make their rects same as caret rect before the first visible
      // character because IME may want to put their UI next to the rect of the
      // invisible character for next input.
      // Note that chars do not contain the invisible characters.
      if (i == 0u && MOZ_LIKELY(offset > aEvent->mInput.mOffset)) {
        const uint32_t offsetInRange =
            offset - CheckedInt<uint32_t>(aEvent->mInput.mOffset).value();
        if (offsetInRange > aEvent->mReply->mRectArray.Length()) {
          LayoutDeviceIntRect caretRectBefore =
              GetCaretRectBefore(rect, lastVisibleFrameWritingMode);
          for ([[maybe_unused]] uint32_t index : IntegerRange<uint32_t>(
                   offsetInRange - aEvent->mReply->mRectArray.Length())) {
            aEvent->mReply->mRectArray.AppendElement(caretRectBefore);
          }
          MOZ_ASSERT(aEvent->mReply->mRectArray.Length() == offsetInRange);
        }
      }

      aEvent->mReply->mRectArray.AppendElement(rect);
      offset++;

      // If it's not a line breaker or the line breaker length is same as
      // XP line breaker's, we need to do nothing for current character.
      wasLineBreaker = chars[i] == '\n';
      if (!wasLineBreaker || kBRLength == 1) {
        continue;
      }

      MOZ_ASSERT(kBRLength == 2);

      // If it's already reached the end of query range, we don't need to do
      // anymore.
      if (offset == kEndOffset) {
        break;
      }

      // If the query range starts from between a line breaker, i.e., it starts
      // between "\r" and "\n", the appended rect was for the "\n".  Therefore,
      // we don't need to append same rect anymore for current "\r\n".
      if (startsBetweenLineBreaker) {
        continue;
      }

      // The appended rect was for "\r" of "\r\n".  Therefore, we need to
      // append same rect for "\n" too because querying rect of "\r" and "\n"
      // should return same rect.  E.g., IME may query previous character's
      // rect of first character of a line.
      aEvent->mReply->mRectArray.AppendElement(rect);
      offset++;
    }
  }

  // If we've not handled some invisible character rects, fill them as caret
  // rect after the last visible character.
  if (!aEvent->mReply->mRectArray.IsEmpty()) {
    const uint32_t offsetInRange =
        offset - CheckedInt<uint32_t>(aEvent->mInput.mOffset).value();
    if (offsetInRange > aEvent->mReply->mRectArray.Length()) {
      LayoutDeviceIntRect caretRectAfter =
          GetCaretRectAfter(aEvent->mReply->mRectArray.LastElement(),
                            lastVisibleFrameWritingMode);
      for ([[maybe_unused]] uint32_t index : IntegerRange<uint32_t>(
               offsetInRange - aEvent->mReply->mRectArray.Length())) {
        aEvent->mReply->mRectArray.AppendElement(caretRectAfter);
      }
      MOZ_ASSERT(aEvent->mReply->mRectArray.Length() == offsetInRange);
    }
  }

  // If the query range is longer than actual content length, we should append
  // caret rect at the end of the content as the last character rect because
  // native IME may want to query character rect at the end of contents for
  // deciding the position of a popup window (e.g., suggest window for next
  // word).  Note that when this method hasn't appended character rects, it
  // means that the offset is too large or the query range is collapsed.
  if (offset < kEndOffset || aEvent->mReply->mRectArray.IsEmpty()) {
    // If we've already retrieved some character rects before current offset,
    // we can guess the last rect from the last character's rect unless it's a
    // line breaker.  (If it's a line breaker, the caret rect is in next line.)
    if (!aEvent->mReply->mRectArray.IsEmpty() && !wasLineBreaker) {
      rect = GetCaretRectAfter(aEvent->mReply->mRectArray.LastElement(),
                               lastVisibleFrameWritingMode);
      aEvent->mReply->mRectArray.AppendElement(rect);
    } else {
      // Note that don't use eQueryCaretRect here because if caret is at the
      // end of the content, it returns actual caret rect instead of computing
      // the rect itself.  It means that the result depends on caret position.
      // So, we shouldn't use it for consistency result in automated tests.
      WidgetQueryContentEvent queryTextRectEvent(eQueryTextRect, *aEvent);
      WidgetQueryContentEvent::Options options(*aEvent);
      queryTextRectEvent.InitForQueryTextRect(offset, 1, options);
      if (NS_WARN_IF(NS_FAILED(OnQueryTextRect(&queryTextRectEvent))) ||
          NS_WARN_IF(queryTextRectEvent.Failed())) {
        return NS_ERROR_FAILURE;
      }
      if (queryTextRectEvent.mReply->mWritingMode.IsVertical()) {
        queryTextRectEvent.mReply->mRect.height = 1;
      } else {
        queryTextRectEvent.mReply->mRect.width = 1;
      }
      aEvent->mReply->mRectArray.AppendElement(
          queryTextRectEvent.mReply->mRect);
    }
  }

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::OnQueryTextRect(WidgetQueryContentEvent* aEvent) {
  // If mLength is 0 (this may be caused by bug of native IME), we should
  // redirect this event to OnQueryCaretRect().
  if (!aEvent->mInput.mLength) {
    return OnQueryCaretRect(aEvent);
  }

  nsresult rv = Init(aEvent);
  if (NS_FAILED(rv)) {
    return rv;
  }

  MOZ_ASSERT(aEvent->mReply->mOffsetAndData.isNothing());
  EditContext* editContext = GetEditContext();
  if (editContext) {
    // Get rectangle using EditContext character bounds
    const uint32_t start = aEvent->mInput.mOffset;
    const uint32_t end = start + aEvent->mInput.mLength;
    AutoTArray<LayoutDeviceIntRect, 8> rects;
    aEvent->mReply->mWritingMode = editContext->WritingMode();
    nsAutoString data;
    editContext->GetTextSubstring(start, end, data);
    aEvent->mReply->mOffsetAndData.emplace(start, data,
                                           OffsetAndDataFor::EditorString);
    Maybe<LayoutDeviceIntRect> selectionBounds =
        editContext->GetSelectionBounds();
    if (selectionBounds && start == editContext->SelectionMinClamped() &&
        end == editContext->SelectionMaxClamped()) {
      // Queried range is EditContext selection, so use selection bounds.
      aEvent->mReply->mRect = *selectionBounds;
      MOZ_ASSERT(aEvent->Succeeded());
      return NS_OK;
    }
    rv = editContext->GetCharacterBounds(start, end, rects);
    // rects will be empty if start >= TextLength()
    if (NS_SUCCEEDED(rv) && !rects.IsEmpty()) {
      // Return union of the character rects.
      LayoutDeviceIntRect boundingRect = rects[0];
      for (size_t i : IntegerRange(1u, rects.Length())) {
        boundingRect = boundingRect.Union(rects[i]);
      }
      aEvent->mReply->mRect = boundingRect;
      MOZ_ASSERT(aEvent->Succeeded());
      return NS_OK;
    }
    // If failed (e.g. web app does not call updateCharacterBounds()), try
    // to get the text from the DOM instead. But for canvas, that certainly
    // won't work, so just use the fallback bounds.
    if (mRootElement->IsHTMLElement(nsGkAtoms::canvas)) {
      aEvent->mReply->mRect = editContext->FallbackBounds();
      MOZ_ASSERT(aEvent->Succeeded());
      return NS_OK;
    }
  }

  Result<DOMRangeAndAdjustedOffsetInFlattenedText, nsresult>
      domRangeAndAdjustedOffsetOrError = ConvertFlatTextOffsetToDOMRange(
          aEvent->mInput.mOffset, aEvent->mInput.mLength, true);
  if (MOZ_UNLIKELY(domRangeAndAdjustedOffsetOrError.isErr())) {
    NS_WARNING("ContentEventHandler::ConvertFlatTextOffsetToDOMRange() failed");
    return NS_ERROR_FAILURE;
  }
  DOMRangeAndAdjustedOffsetInFlattenedText domRangeAndAdjustedOffset =
      domRangeAndAdjustedOffsetOrError.unwrap();
  nsString string;
  if (NS_WARN_IF(NS_FAILED(
          GenerateFlatTextContent(domRangeAndAdjustedOffset.mRange, string)))) {
    return NS_ERROR_FAILURE;
  }
  if (!editContext) {
    aEvent->mReply->mOffsetAndData.emplace(
        domRangeAndAdjustedOffset.mAdjustedOffset, string,
        OffsetAndDataFor::EditorString);
  } else {
    MOZ_ASSERT(aEvent->mReply->mOffsetAndData.isSome(),
               "Should have been initialized above.");
  }

  // used to iterate over all contents and their frames
  PostContentIterator postOrderIter;
  rv = postOrderIter.Init(domRangeAndAdjustedOffset.mRange.Start().AsRaw(),
                          domRangeAndAdjustedOffset.mRange.End().AsRaw());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_FAILURE;
  }

  // Get the first frame which causes some text after the offset.
  FrameAndNodeOffset firstFrame =
      GetFirstFrameInRangeForTextRect(domRangeAndAdjustedOffset.mRange);

  // If GetFirstFrameInRangeForTextRect() does not return valid frame, that
  // means that there are no visible frames having text or the offset reached
  // the end of contents.
  if (!firstFrame.IsValid()) {
    nsAutoString allText;
    rv = GenerateFlatTextContent(mRootElement, allText);
    // If the offset doesn't reach the end of contents but there is no frames
    // for the node, that means that current offset's node is hidden by CSS or
    // something.  Ideally, we should handle it with the last visible text
    // node's last character's rect, but it's not usual cases in actual web
    // services.  Therefore, currently, we should make this case fail.
    if (NS_WARN_IF(NS_FAILED(rv)) ||
        static_cast<uint32_t>(aEvent->mInput.mOffset) < allText.Length()) {
      return NS_ERROR_FAILURE;
    }

    // Look for the last frame which should be included text rects.
    rv = domRangeAndAdjustedOffset.mRange.SelectNodeContents(mRootElement);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return NS_ERROR_UNEXPECTED;
    }
    nsRect rect;
    FrameAndNodeOffset lastFrame =
        GetLastFrameInRangeForTextRect(domRangeAndAdjustedOffset.mRange);
    // If there is at least one frame which can be used for computing a rect
    // for a character or a line breaker, we should use it for guessing the
    // caret rect at the end of the contents.
    nsPresContext* presContext;
    if (lastFrame) {
      presContext = lastFrame->PresContext();
      if (NS_WARN_IF(!lastFrame->GetContent())) {
        return NS_ERROR_FAILURE;
      }
      FrameRelativeRect relativeRect;
      // If there is a <br> frame at the end, it represents an empty line at
      // the end with moz-<br> or content <br> in a block level element.
      if (lastFrame->IsBrFrame()) {
        relativeRect = GetLineBreakerRectBefore(lastFrame);
      }
      // If there is a text frame at the end, use its information.
      else if (lastFrame->IsTextFrame()) {
        const Text* textNode = Text::FromNode(lastFrame->GetContent());
        MOZ_ASSERT(textNode);
        if (textNode) {
          relativeRect = GuessLineBreakerRectAfter(*textNode);
        }
      }
      // If there is an empty frame which is neither a text frame nor a <br>
      // frame at the end, guess caret rect in it.
      else {
        relativeRect = GuessFirstCaretRectIn(lastFrame);
      }
      if (NS_WARN_IF(!relativeRect.IsValid())) {
        return NS_ERROR_FAILURE;
      }
      rect = relativeRect.RectRelativeTo(lastFrame);
      rv = ConvertToRootRelativeOffset(lastFrame, rect);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      aEvent->mReply->mWritingMode = lastFrame->GetWritingMode();
    }
    // Otherwise, if there are no contents in mRootElement, guess caret rect in
    // its frame (with its font height and content box).
    else {
      nsIFrame* rootContentFrame = mRootElement->GetPrimaryFrame();
      if (NS_WARN_IF(!rootContentFrame)) {
        return NS_ERROR_FAILURE;
      }
      presContext = rootContentFrame->PresContext();
      FrameRelativeRect relativeRect = GuessFirstCaretRectIn(rootContentFrame);
      if (NS_WARN_IF(!relativeRect.IsValid())) {
        return NS_ERROR_FAILURE;
      }
      rect = relativeRect.RectRelativeTo(rootContentFrame);
      rv = ConvertToRootRelativeOffset(rootContentFrame, rect);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      aEvent->mReply->mWritingMode = rootContentFrame->GetWritingMode();
    }
    aEvent->mReply->mRect = LayoutDeviceIntRect::FromAppUnitsToOutside(
        rect, presContext->AppUnitsPerDevPixel());
    if (nsPresContext* rootContext =
            presContext->GetInProcessRootContentDocumentPresContext()) {
      aEvent->mReply->mRect =
          RoundedOut(ViewportUtils::DocumentRelativeLayoutToVisual(
              aEvent->mReply->mRect, rootContext->PresShell()));
    }
    EnsureNonEmptyRect(aEvent->mReply->mRect);

    MOZ_ASSERT(aEvent->Succeeded());
    return NS_OK;
  }

  nsRect rect, frameRect;
  nsPoint ptOffset;

  // If the first frame is a text frame, the result should be computed with
  // the frame's rect but not including the rect before start point of the
  // queried range.
  if (firstFrame->IsTextFrame()) {
    rect.SetRect(nsPoint(0, 0), firstFrame->GetRect().Size());
    frameRect = rect;
    // Exclude the rect before start point of the queried range.
    firstFrame->GetPointFromOffset(firstFrame.mOffsetInNode, &ptOffset);
    if (firstFrame->GetWritingMode().IsVertical()) {
      rect.y += ptOffset.y;
      rect.height -= ptOffset.y;
    } else {
      rect.x += ptOffset.x;
      rect.width -= ptOffset.x;
    }
    // The character offset is relative to the untransformed text frame, so
    // clip the frame before converting it through CSS transforms.  Mixing a
    // transformed frame rect with an untransformed offset shifts ranges in
    // scaled text and gives them the wrong width.
    rv = ConvertToRootRelativeOffset(firstFrame, rect);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    rv = ConvertToRootRelativeOffset(firstFrame, frameRect);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }
  // If first frame causes a line breaker but it's not a <br> frame, we cannot
  // compute proper rect only with the frame because typically caret is at
  // right of the last character of it.  For example, if caret is after "c" of
  // |<p>abc</p><p>def</p>|, IME may query a line breaker's rect after "c".
  // Then, if we compute it only with the 2nd <p>'s block frame, the result
  // will be:
  //  +-<p>--------------------------------+
  //  |abc                                 |
  //  +------------------------------------+
  //
  // I+-<p>--------------------------------+
  //  |def                                 |
  //  +------------------------------------+
  // However, users expect popup windows of IME should be positioned at
  // right-bottom of "c" like this:
  //  +-<p>--------------------------------+
  //  |abcI                                |
  //  +------------------------------------+
  //
  //  +-<p>--------------------------------+
  //  |def                                 |
  //  +------------------------------------+
  // Therefore, if the first frame isn't a <br> frame and there is a visible
  // text node before the first node in the queried range, we should compute the
  // first rect with the previous character's rect.
  else if (!firstFrame->IsBrFrame() &&
           domRangeAndAdjustedOffset.mLastTextNode &&
           domRangeAndAdjustedOffset.mLastTextNode->GetPrimaryFrame()) {
    FrameRelativeRect brRectAfterLastChar =
        GuessLineBreakerRectAfter(*domRangeAndAdjustedOffset.mLastTextNode);
    if (NS_WARN_IF(!brRectAfterLastChar.IsValid())) {
      return NS_ERROR_FAILURE;
    }
    rect = brRectAfterLastChar.mRect;
    rv = ConvertToRootRelativeOffset(brRectAfterLastChar.mBaseFrame, rect);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    frameRect = rect;
  }
  // Otherwise, we need to compute the line breaker's rect only with the
  // first frame's rect.  But this may be unexpected.  For example,
  // |<div contenteditable>[<p>]abc</p></div>|.  In this case, caret is before
  // "a", therefore, users expect the rect left of "a".  However, we don't
  // have enough information about the next character here and this isn't
  // usual case (e.g., IME typically tries to query the rect of "a" or caret
  // rect for computing its popup position).  Therefore, we shouldn't do
  // more complicated hack here unless we'll get some bug reports actually.
  else {
    FrameRelativeRect relativeRect = GetLineBreakerRectBefore(firstFrame);
    if (NS_WARN_IF(!relativeRect.IsValid())) {
      return NS_ERROR_FAILURE;
    }
    rect = relativeRect.RectRelativeTo(firstFrame);
    rv = ConvertToRootRelativeOffset(firstFrame, rect);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    frameRect = rect;
  }
  // UnionRect() requires non-empty rect.  So, let's make sure to get non-emtpy
  // rect from the first frame.
  EnsureNonEmptyRect(rect);

  // Get the last frame which causes some text in the range.
  FrameAndNodeOffset lastFrame =
      GetLastFrameInRangeForTextRect(domRangeAndAdjustedOffset.mRange);
  if (NS_WARN_IF(!lastFrame.IsValid())) {
    return NS_ERROR_FAILURE;
  }

  // iterate over all covered frames
  for (nsIFrame* frame = firstFrame; frame != lastFrame;) {
    frame = frame->GetNextContinuation();
    if (!frame) {
      do {
        postOrderIter.Next();
        nsINode* node = postOrderIter.GetCurrentNode();
        if (!node) {
          break;
        }
        if (!node->IsContent()) {
          continue;
        }
        nsIFrame* primaryFrame = node->AsContent()->GetPrimaryFrame();
        // The node may be hidden by CSS.
        if (!primaryFrame) {
          continue;
        }
        // We should take only text frame's rect and br frame's rect.  We can
        // always use frame rect of text frame and GetLineBreakerRectBefore()
        // can return exactly correct rect only for <br> frame for now.  On the
        // other hand, GetLineBreakRectBefore() returns guessed caret rect for
        // the other frames.  We shouldn't include such odd rect to the result.
        if (primaryFrame->IsTextFrame() || primaryFrame->IsBrFrame()) {
          frame = primaryFrame;
        }
      } while (!frame && !postOrderIter.IsDone());
      if (!frame) {
        break;
      }
    }
    if (frame->IsTextFrame()) {
      frameRect.SetRect(nsPoint(0, 0), frame->GetRect().Size());
    } else {
      MOZ_ASSERT(frame->IsBrFrame());
      FrameRelativeRect relativeRect = GetLineBreakerRectBefore(frame);
      if (NS_WARN_IF(!relativeRect.IsValid())) {
        return NS_ERROR_FAILURE;
      }
      frameRect = relativeRect.RectRelativeTo(frame);
    }
    rv = ConvertToRootRelativeOffset(frame, frameRect);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    // UnionRect() requires non-empty rect.  So, let's make sure to get
    // non-emtpy rect from the frame.
    EnsureNonEmptyRect(frameRect);
    if (frame != lastFrame) {
      // not last frame, so just add rect to previous result
      rect.UnionRect(rect, frameRect);
    }
  }

  // Shrink the last frame for cutting off the text after the query range.
  if (lastFrame->IsTextFrame()) {
    nsRect lastFrameRect(nsPoint(0, 0), lastFrame->GetRect().Size());
    lastFrame->GetPointFromOffset(lastFrame.mOffsetInNode, &ptOffset);
    if (lastFrame->GetWritingMode().IsVertical()) {
      lastFrameRect.height -= lastFrame->GetRect().height - ptOffset.y;
    } else {
      lastFrameRect.width -= lastFrame->GetRect().width - ptOffset.x;
    }
    // UnionRect() requires non-empty rect.  So, let's make sure to get
    // non-empty rect from the last frame.
    EnsureNonEmptyRect(lastFrameRect);
    // As with the start offset above, apply the end offset in frame-local
    // coordinates before converting through CSS transforms.
    rv = ConvertToRootRelativeOffset(lastFrame, lastFrameRect);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (firstFrame.mFrame == lastFrame.mFrame) {
      rect.IntersectRect(rect, lastFrameRect);
    } else {
      rect.UnionRect(rect, lastFrameRect);
    }
  }

  nsPresContext* presContext = lastFrame->PresContext();
  aEvent->mReply->mRect = LayoutDeviceIntRect::FromAppUnitsToOutside(
      rect, presContext->AppUnitsPerDevPixel());
  if (nsPresContext* rootContext =
          presContext->GetInProcessRootContentDocumentPresContext()) {
    aEvent->mReply->mRect =
        RoundedOut(ViewportUtils::DocumentRelativeLayoutToVisual(
            aEvent->mReply->mRect, rootContext->PresShell()));
  }
  // Returning empty rect may cause native IME confused, let's make sure to
  // return non-empty rect.
  EnsureNonEmptyRect(aEvent->mReply->mRect);
  aEvent->mReply->mWritingMode = lastFrame->GetWritingMode();

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::OnQueryEditorRect(
    WidgetQueryContentEvent* aEvent) {
  nsresult rv = Init(aEvent);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (EditContext* editContext = GetEditContext()) {
    if (Maybe<LayoutDeviceIntRect> controlBounds =
            editContext->GetControlBounds()) {
      aEvent->mReply->mRect = *controlBounds;
      MOZ_ASSERT(aEvent->Succeeded());
      return NS_OK;
    }
  }

  if (NS_WARN_IF(NS_FAILED(QueryContentRect(mRootElement, aEvent)))) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::OnQueryCaretRect(
    WidgetQueryContentEvent* aEvent) {
  nsresult rv = Init(aEvent);
  if (NS_FAILED(rv)) {
    return rv;
  }

  EditContext* editContext = GetEditContext();
  if (editContext && mSelection->GetType() == SelectionType::eNormal &&
      editContext->SelectionIsCollapsed() &&
      editContext->SelectionStartClamped() == aEvent->mInput.mOffset) {
    if (Maybe<LayoutDeviceIntRect> selectionBounds =
            editContext->GetSelectionBounds()) {
      // Query is for the "real" caret rect, so we can use
      // the EditContext's selection bounds.
      aEvent->mReply->mRect = *selectionBounds;
      aEvent->mReply->mOffsetAndData.emplace(aEvent->mInput.mOffset,
                                             EmptyString(),
                                             OffsetAndDataFor::SelectedString);
      aEvent->mReply->mWritingMode = editContext->WritingMode();
      MOZ_ASSERT(aEvent->Succeeded());
      return NS_OK;
    }
  }

  // When the selection is collapsed and the queried offset is current caret
  // position, we should return the "real" caret rect.
  if (mSelection->IsCollapsed()) {
    nsRect caretRect;
    nsIFrame* caretFrame = nsCaret::GetGeometry(mSelection, &caretRect);
    if (caretFrame) {
      Result<uint32_t, nsresult> offsetOrError =
          GetStartOffset(mFirstSelectedSimpleRange);
      if (NS_WARN_IF(offsetOrError.isErr())) {
        return offsetOrError.unwrapErr();
      }
      if (offsetOrError.inspect() == aEvent->mInput.mOffset) {
        rv = ConvertToRootRelativeOffset(caretFrame, caretRect);
        NS_ENSURE_SUCCESS(rv, rv);
        nsPresContext* presContext = caretFrame->PresContext();
        aEvent->mReply->mRect = LayoutDeviceIntRect::FromAppUnitsToOutside(
            caretRect, presContext->AppUnitsPerDevPixel());
        if (nsPresContext* rootContext =
                presContext->GetInProcessRootContentDocumentPresContext()) {
          aEvent->mReply->mRect =
              RoundedOut(ViewportUtils::DocumentRelativeLayoutToVisual(
                  aEvent->mReply->mRect, rootContext->PresShell()));
        }
        // Returning empty rect may cause native IME confused, let's make sure
        // to return non-empty rect.
        EnsureNonEmptyRect(aEvent->mReply->mRect);
        aEvent->mReply->mWritingMode = caretFrame->GetWritingMode();
        aEvent->mReply->mOffsetAndData.emplace(
            aEvent->mInput.mOffset, EmptyString(),
            OffsetAndDataFor::SelectedString);

        MOZ_ASSERT(aEvent->Succeeded());
        return NS_OK;
      }
    }
  }

  // Otherwise, we should guess the caret rect from the character's rect.
  WidgetQueryContentEvent queryTextRectEvent(eQueryTextRect, *aEvent);
  WidgetQueryContentEvent::Options options(*aEvent);
  queryTextRectEvent.InitForQueryTextRect(aEvent->mInput.mOffset, 1, options);
  if (NS_WARN_IF(NS_FAILED(OnQueryTextRect(&queryTextRectEvent))) ||
      NS_WARN_IF(queryTextRectEvent.Failed())) {
    return NS_ERROR_FAILURE;
  }
  queryTextRectEvent.mReply->TruncateData();
  aEvent->mReply->mOffsetAndData =
      std::move(queryTextRectEvent.mReply->mOffsetAndData);
  aEvent->mReply->mWritingMode =
      std::move(queryTextRectEvent.mReply->mWritingMode);
  aEvent->mReply->mRect = GetCaretRectBefore(queryTextRectEvent.mReply->mRect,
                                             aEvent->mReply->mWritingMode);

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::OnQueryContentState(
    WidgetQueryContentEvent* aEvent) {
  if (NS_FAILED(Init(aEvent))) {
    return NS_ERROR_FAILURE;
  }
  MOZ_ASSERT(aEvent->mReply.isSome());
  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::OnQuerySelectionAsTransferable(
    WidgetQueryContentEvent* aEvent) {
  nsresult rv = Init(aEvent);
  if (NS_FAILED(rv)) {
    return rv;
  }

  MOZ_ASSERT(aEvent->mReply.isSome());

  if (mSelection->IsCollapsed()) {
    MOZ_ASSERT(!aEvent->mReply->mTransferable);
    return NS_OK;
  }

  if (NS_WARN_IF(NS_FAILED(nsCopySupport::GetTransferableForSelection(
          mSelection, mDocument,
          getter_AddRefs(aEvent->mReply->mTransferable))))) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::OnQueryCharacterAtPoint(
    WidgetQueryContentEvent* aEvent) {
  nsresult rv = Init(aEvent);
  if (NS_FAILED(rv)) {
    return rv;
  }

  MOZ_ASSERT(aEvent->mReply->mOffsetAndData.isNothing());
  MOZ_ASSERT(aEvent->mReply->mTentativeCaretOffset.isNothing());

  if (EditContext* editContext = GetEditContext()) {
    AutoTArray<LayoutDeviceIntRect, 8> rects;
    // The character bounds offset/length can be set to anything,
    // so we have to be careful and clamp to the text length.
    const uint32_t start = std::min(editContext->TextLength(),
                                    editContext->CharacterBoundsRangeStart());
    const CheckedUint32 checkedEnd =
        CheckedUint32(start) + editContext->CharacterBoundsLength();
    const uint32_t end =
        std::min(checkedEnd.isValid() ? checkedEnd.value() : UINT32_MAX,
                 editContext->TextLength());
    rv = editContext->GetCharacterBounds(start, end, rects);
    if (NS_SUCCEEDED(rv)) {
      for (uint32_t i : IntegerRange(start, end)) {
        const LayoutDeviceIntRect rect = rects[i - start];
        if (rect.Contains(aEvent->mRefPoint)) {
          nsAutoString string;
          editContext->GetTextSubstring(i, i + 1, string);
          aEvent->mReply->mOffsetAndData.emplace(i, string);
          aEvent->mReply->mRect = rect;
          // XXX: Should we adjust this based on where in the rect the point is
          //      and the directionality of the text?
          aEvent->mReply->mTentativeCaretOffset = Some(i);
          return NS_OK;
        }
      }
      return NS_OK;
    }
    // If failed (e.g. web app does not call updateCharacterBounds()), try
    // to get the text from the DOM instead. But for canvas, that certainly
    // won't work, so there's nothing we can do.
    if (mRootElement->IsHTMLElement(nsGkAtoms::canvas)) {
      return NS_ERROR_FAILURE;
    }
  }

  PresShell* presShell = mDocument->GetPresShell();
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);
  nsIFrame* rootFrame = presShell->GetRootFrame();
  NS_ENSURE_TRUE(rootFrame, NS_ERROR_FAILURE);
  nsIWidget* rootWidget = rootFrame->GetNearestWidget();
  NS_ENSURE_TRUE(rootWidget, NS_ERROR_FAILURE);

  // The root frame's widget might be different, e.g., the event was fired on
  // a popup but the rootFrame is the document root.
  if (rootWidget != aEvent->mWidget) {
    MOZ_ASSERT(aEvent->mWidget, "The event must have the widget");
    rootFrame = aEvent->mWidget->GetFrame();
    NS_ENSURE_TRUE(rootFrame, NS_ERROR_FAILURE);
    rootWidget = rootFrame->GetNearestWidget();
    NS_ENSURE_TRUE(rootWidget, NS_ERROR_FAILURE);
  }

  WidgetQueryContentEvent queryCharAtPointOnRootWidgetEvent(
      true, eQueryCharacterAtPoint, rootWidget);
  queryCharAtPointOnRootWidgetEvent.mRefPoint = aEvent->mRefPoint;
  if (rootWidget != aEvent->mWidget) {
    queryCharAtPointOnRootWidgetEvent.mRefPoint +=
        aEvent->mWidget->WidgetToScreenOffset() -
        rootWidget->WidgetToScreenOffset();
  }
  nsPoint ptInRoot = nsLayoutUtils::GetEventCoordinatesRelativeTo(
      &queryCharAtPointOnRootWidgetEvent, RelativeTo{rootFrame});

  nsIFrame* targetFrame =
      nsLayoutUtils::GetFrameForPoint(RelativeTo{rootFrame}, ptInRoot);
  if (!targetFrame || !targetFrame->GetContent() ||
      !targetFrame->GetContent()->IsInclusiveDescendantOf(mRootElement)) {
    // There is no character at the point.
    MOZ_ASSERT(aEvent->Succeeded());
    return NS_OK;
  }
  // GetFrameForPoint() takes CSS transforms into account when choosing the
  // target frame. Convert the point through the same transform chain before
  // asking that frame for a character offset.
  nsPoint ptInTarget = ptInRoot;
  if (NS_WARN_IF(nsLayoutUtils::TransformPoint(
                     RelativeTo{rootFrame}, RelativeTo{targetFrame},
                     ptInTarget) != nsLayoutUtils::TRANSFORM_SUCCEEDED)) {
    MOZ_ASSERT(aEvent->Succeeded());
    return NS_OK;
  }

  nsIFrame::ContentOffsets tentativeCaretOffsets =
      targetFrame->GetContentOffsetsFromPoint(ptInTarget);
  if (!tentativeCaretOffsets.content ||
      !tentativeCaretOffsets.content->IsInclusiveDescendantOf(mRootElement)) {
    // There is no character nor tentative caret point at the point.
    MOZ_ASSERT(aEvent->Succeeded());
    return NS_OK;
  }

  Result<uint32_t, nsresult> tentativeCaretOffsetOrError =
      GetFlatTextLengthInRange(RawNodePosition(mRootElement, 0u),
                               RawNodePosition(tentativeCaretOffsets),
                               mRootElement);
  if (NS_WARN_IF(tentativeCaretOffsetOrError.isErr())) {
    return tentativeCaretOffsetOrError.unwrapErr();
  }

  aEvent->mReply->mTentativeCaretOffset.emplace(
      tentativeCaretOffsetOrError.inspect());
  if (!targetFrame->IsTextFrame()) {
    // There is no character at the point but there is tentative caret point.
    MOZ_ASSERT(aEvent->Succeeded());
    return NS_OK;
  }

  nsTextFrame* textframe = static_cast<nsTextFrame*>(targetFrame);
  nsIFrame::ContentOffsets contentOffsets =
      textframe->GetCharacterOffsetAtFramePoint(ptInTarget);
  NS_ENSURE_TRUE(contentOffsets.content, NS_ERROR_FAILURE);
  Result<uint32_t, nsresult> offsetOrError =
      GetFlatTextLengthInRange(RawNodePosition(mRootElement, 0u),
                               RawNodePosition(contentOffsets), mRootElement);
  if (NS_WARN_IF(offsetOrError.isErr())) {
    return offsetOrError.unwrapErr();
  }

  WidgetQueryContentEvent queryTextRectEvent(true, eQueryTextRect,
                                             aEvent->mWidget);
  WidgetQueryContentEvent::Options options(*aEvent);
  queryTextRectEvent.InitForQueryTextRect(offsetOrError.inspect(), 1, options);
  if (NS_WARN_IF(NS_FAILED(OnQueryTextRect(&queryTextRectEvent))) ||
      NS_WARN_IF(queryTextRectEvent.Failed())) {
    return NS_ERROR_FAILURE;
  }

  aEvent->mReply->mOffsetAndData =
      std::move(queryTextRectEvent.mReply->mOffsetAndData);
  aEvent->mReply->mRect = queryTextRectEvent.mReply->mRect;

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::QueryHittestImpl(WidgetQueryContentEvent* aEvent,
                                               bool aFlushLayout,
                                               bool aPerformRetargeting,
                                               Element** aContentUnderMouse) {
  NS_ASSERTION(aEvent, "aEvent must not be null");

  nsresult rv = InitBasic();
  if (NS_FAILED(rv)) {
    return rv;
  }

  NS_ENSURE_TRUE(aEvent->mWidget, NS_ERROR_FAILURE);

  PresShell* presShell = mDocument->GetPresShell();
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);
  nsIFrame* docFrame = presShell->GetRootFrame();
  NS_ENSURE_TRUE(docFrame, NS_ERROR_FAILURE);

  LayoutDeviceIntPoint eventLoc =
      aEvent->mRefPoint + aEvent->mWidget->WidgetToScreenOffset();
  CSSIntRect docFrameRect = docFrame->GetScreenRect();
  CSSIntPoint eventLocCSS(
      docFrame->PresContext()->DevPixelsToIntCSSPixels(eventLoc.x) -
          docFrameRect.x,
      docFrame->PresContext()->DevPixelsToIntCSSPixels(eventLoc.y) -
          docFrameRect.y);
  RefPtr<Element> contentUnderMouse = mDocument->ElementFromPointHelper(
      eventLocCSS.x, eventLocCSS.y, false, false, ViewportType::Visual,
      aPerformRetargeting);

  contentUnderMouse.forget(aContentUnderMouse);
  return NS_OK;
}

nsresult ContentEventHandler::OnQueryDOMWidgetHittest(
    WidgetQueryContentEvent* aEvent) {
  aEvent->mReply->mWidgetIsHit = false;
  RefPtr<Element> contentUnderMouse;
  nsresult rv = QueryHittestImpl(aEvent, true /* flushLayout */,
                                 true /* performRetargeting */,
                                 getter_AddRefs(contentUnderMouse));
  NS_ENSURE_SUCCESS(rv, rv);
  if (contentUnderMouse) {
    if (nsIFrame* targetFrame = contentUnderMouse->GetPrimaryFrame()) {
      if (aEvent->mWidget == targetFrame->GetNearestWidget()) {
        aEvent->mReply->mWidgetIsHit = true;
      }
    }
  }

  MOZ_ASSERT(aEvent->Succeeded());
  return NS_OK;
}

nsresult ContentEventHandler::OnQueryDropTargetHittest(
    WidgetQueryContentEvent* aEvent) {
  RefPtr<Element> contentUnderMouse;
  nsresult rv = QueryHittestImpl(aEvent, true /* flushLayout */,
                                 false /* performRetargeting */,
                                 getter_AddRefs(contentUnderMouse));
  NS_ENSURE_SUCCESS(rv, rv);
  aEvent->EmplaceReply();
  aEvent->mReply->mDropElement = contentUnderMouse;
  aEvent->mReply->mDropFrame =
      mDocument->GetPresShell()->GetCurrentEventFrame();
  return NS_OK;
}

/* static */
Result<uint32_t, nsresult> ContentEventHandler::GetFlatTextLengthInRange(
    const RawNodePosition& aStartPosition, const RawNodePosition& aEndPosition,
    const Element* aRootElement) {
  if (NS_WARN_IF(!aRootElement) || NS_WARN_IF(!aStartPosition.IsSet()) ||
      NS_WARN_IF(!aEndPosition.IsSet())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  if (aStartPosition == aEndPosition) {
    return 0u;
  }

  MOZ_ASSERT(!aStartPosition.GetContainer()->IsBeingRemoved());
  MOZ_ASSERT(!aEndPosition.GetContainer()->IsBeingRemoved());

  UnsafePreContentIterator preOrderIter;

  // Working with ContentIterator, we may need to adjust the end position for
  // including it forcibly.
  RawNodePosition endPosition(aEndPosition);

  SimpleRange prevSimpleRange;
  nsresult rv = prevSimpleRange.SetStart(aStartPosition.AsRaw());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Err(rv);
  }

  // When the end position is immediately after non-root element's open tag,
  // we need to include a line break caused by the open tag.
  if (endPosition.GetContainer() != aRootElement &&
      endPosition.IsImmediatelyAfterOpenTag()) {
    if (endPosition.GetContainer()->HasChildren()) {
      // When the end node has some children, move the end position to before
      // the open tag of its first child.
      nsIContent* const firstChild =
          endPosition.GetContainer()->GetFirstChild();
      if (NS_WARN_IF(!firstChild)) {
        return Err(NS_ERROR_FAILURE);
      }
      endPosition = RawNodePosition::Before(*firstChild);
    } else {
      // When the end node is empty, move the end position after the node.
      if (NS_WARN_IF(!endPosition.GetContainer()->IsContent())) {
        return Err(NS_ERROR_FAILURE);
      }
      nsIContent* const parentContent = endPosition.GetContainer()->GetParent();
      if (NS_WARN_IF(!parentContent)) {
        return Err(NS_ERROR_FAILURE);
      }
      endPosition =
          RawNodePosition::After(*endPosition.GetContainer()->AsContent());
    }
  }

  if (endPosition.IsSetAndValid()) {
    // Offset is within node's length; set end of range to that offset
    nsresult rv = prevSimpleRange.SetEnd(endPosition.AsRaw());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
    rv = preOrderIter.Init(prevSimpleRange.Start().AsRaw(),
                           prevSimpleRange.End().AsRaw());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
  } else if (endPosition.GetContainer() != aRootElement) {
    // Offset is past node's length; set end of range to end of node
    nsresult rv = prevSimpleRange.SetEndAfter(
        nsIContent::FromNode(endPosition.GetContainer()));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
    rv = preOrderIter.Init(prevSimpleRange.Start().AsRaw(),
                           prevSimpleRange.End().AsRaw());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
  } else {
    // Offset is past the root node; set end of range to end of root node
    nsresult rv = preOrderIter.Init(const_cast<Element*>(aRootElement));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Err(rv);
    }
  }

  uint32_t length = 0;
  for (; !preOrderIter.IsDone(); preOrderIter.Next()) {
    nsIContent* const content =
        nsIContent::FromNode(preOrderIter.GetCurrentNode());
    if (!content) [[unlikely]] {
      continue;
    }
    if (const Text* textNode = Text::FromNode(content)) {
      // Note: our range always starts from offset 0
      if (content == endPosition.GetContainer()) {
        // NOTE: We should have an offset here, as endPosition.GetContainer() is
        // a nsINode::eTEXT, which always has an offset.
        length += GetTextLength(
            *textNode,
            *endPosition.Offset(
                RawNodePosition::OffsetFilter::kValidOrInvalidOffsets));
      } else {
        length += GetTextLength(*textNode);
      }
    } else if (ShouldBreakLineBefore(*content, aRootElement)) {
      // If the start position is start of this node but doesn't include the
      // open tag, don't append the line break length.
      if (content == aStartPosition.GetContainer() &&
          !aStartPosition.IsBeforeOpenTag()) {
        continue;
      }
      // If the end position is before the open tag, don't append the line
      // break length.
      if (content == endPosition.GetContainer() &&
          endPosition.IsBeforeOpenTag()) {
        continue;
      }
      length += kBRLength;
    }
  }
  return length;
}

template <typename SimpleRangeType>
Result<uint32_t, nsresult> ContentEventHandler::GetStartOffset(
    const SimpleRangeType& aSimpleRange) const {
  // To match the "no skip start" hack in ContentIterator::Init, when range
  // offset is 0 and the range node is not a container, we have to assume the
  // range _includes_ the node, which means the start offset should _not_
  // include the node.
  //
  // For example, for this content: <br>abc, and range (<br>, 0)-("abc", 1), the
  // range includes the linebreak from <br>, so the start offset should _not_
  // include <br>, and the start offset should be 0.
  //
  // However, for this content: <p/>abc, and range (<p>, 0)-("abc", 1), the
  // range does _not_ include the linebreak from <p> because <p> is a container,
  // so the start offset _should_ include <p>, and the start offset should be 1.

  nsINode* startNode = aSimpleRange.GetStartContainer();
  bool startIsContainer = true;
  if (startNode->IsHTMLElement()) {
    nsAtom* name = startNode->NodeInfo()->NameAtom();
    startIsContainer =
        nsHTMLElement::IsContainer(nsHTMLTags::AtomTagToId(name));
  }
  RawNodePosition startPos(startNode, aSimpleRange.StartOffset());
  startPos.mAfterOpenTag = startIsContainer;
  return GetFlatTextLengthInRange(RawNodePosition(mRootElement, 0u), startPos,
                                  mRootElement);
}

nsresult ContentEventHandler::AdjustCollapsedRangeMaybeIntoTextNode(
    SimpleRange& aSimpleRange) {
  MOZ_ASSERT(aSimpleRange.Collapsed());

  if (!aSimpleRange.Collapsed()) {
    return NS_ERROR_INVALID_ARG;
  }

  const RangeBoundary& startPoint = aSimpleRange.Start();
  if (NS_WARN_IF(!startPoint.IsSet())) {
    return NS_ERROR_INVALID_ARG;
  }

  // If the node does not have children like a text node, we don't need to
  // modify aSimpleRange.
  if (!startPoint.GetContainer()->HasChildren()) {
    return NS_OK;
  }

  // If the container is not a text node but it has a text node at the offset,
  // we should adjust the range into the text node.
  // NOTE: This is emulating similar situation of EditorBase.
  if (startPoint.IsStartOfContainer()) {
    // If the range is the start of the container, adjusted the range to the
    // start of the first child.
    nsIContent* const firstChild = startPoint.GetContainer()->GetFirstChild();
    if (!firstChild->IsText()) {
      return NS_OK;
    }
    nsresult rv =
        aSimpleRange.CollapseTo(RawRangeBoundary::StartOfParent(*firstChild));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    return NS_OK;
  }

  if (!startPoint.IsSetAndValid()) {
    return NS_OK;
  }

  // If start of the range is next to a child node, adjust the range to the
  // end of the previous child (i.e., startPoint.Ref()).
  if (!startPoint.Ref()->IsText()) {
    return NS_OK;
  }
  nsresult rv =
      aSimpleRange.CollapseTo(RawRangeBoundary::EndOfParent(*startPoint.Ref()));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  return NS_OK;
}

nsresult ContentEventHandler::ConvertToRootRelativeOffset(nsIFrame* aFrame,
                                                          nsRect& aRect) {
  NS_ASSERTION(aFrame, "aFrame must not be null");

  nsPresContext* thisPC = aFrame->PresContext();
  nsPresContext* rootPC = thisPC->GetRootPresContext();
  if (NS_WARN_IF(!rootPC)) {
    return NS_ERROR_FAILURE;
  }
  nsIFrame* rootFrame = rootPC->PresShell()->GetRootFrame();
  if (NS_WARN_IF(!rootFrame)) {
    return NS_ERROR_FAILURE;
  }

  aRect = nsLayoutUtils::TransformFrameRectToAncestor(aFrame, aRect, rootFrame);

  // TransformFrameRectToAncestor returned the rect in the ancestor's appUnits,
  // but we want it in aFrame's units (in case of different full-zoom factors),
  // so convert back.
  aRect = aRect.ScaleToOtherAppUnitsRoundOut(rootPC->AppUnitsPerDevPixel(),
                                             thisPC->AppUnitsPerDevPixel());

  return NS_OK;
}

static void AdjustRangeForSelection(const Element* aRootElement,
                                    nsINode** aNode,
                                    Maybe<uint32_t>* aNodeOffset) {
  nsINode* node = *aNode;
  Maybe<uint32_t> nodeOffset = *aNodeOffset;
  if (aRootElement == node || NS_WARN_IF(!node->GetParent()) ||
      !node->IsText()) {
    return;
  }

  // When the offset is at the end of the text node, set it to after the
  // text node, to make sure the caret is drawn on a new line when the last
  // character of the text node is '\n' in <textarea>.
  const uint32_t textLength = node->AsContent()->TextLength();
  MOZ_ASSERT(nodeOffset.isNothing() || *nodeOffset <= textLength,
             "Offset is past length of text node");
  if (nodeOffset.isNothing() || *nodeOffset != textLength) {
    return;
  }

  Element* rootParentElement = aRootElement->GetParentElement();
  if (NS_WARN_IF(!rootParentElement)) {
    return;
  }
  // If the root node is not an anonymous div of <textarea>, we don't need to
  // do this hack.  If you did this, ContentEventHandler couldn't distinguish
  // if the range includes open tag of the next node in some cases, e.g.,
  // textNode]<p></p> vs. textNode<p>]</p>
  if (!rootParentElement->IsHTMLElement(nsGkAtoms::textarea)) {
    return;
  }

  // If the node is being removed from its parent, it holds the ex-parent,
  // but the parent have already removed the child from its child chain.
  // Therefore `ComputeIndexOf` may fail, but I don't want to make Beta/Nightly
  // crash at accessing `Maybe::operator*` so that here checks `isSome`, but
  // crashing only in debug builds may help to debug something complicated
  // situation, therefore, `MOZ_ASSERT` is put here.
  *aNode = node->GetParent();
  Maybe<uint32_t> index = (*aNode)->ComputeIndexOf(node);
  MOZ_ASSERT(index.isSome());
  if (index.isSome()) {
    MOZ_ASSERT(*index != UINT32_MAX);
    *aNodeOffset = Some(*index + 1u);
  } else {
    *aNodeOffset = Some(0u);
  }
}

nsresult ContentEventHandler::OnSelectionEvent(WidgetSelectionEvent* aEvent) {
  aEvent->mSucceeded = false;

  // Get selection to manipulate
  // XXX why do we need to get them from ISM? This method should work fine
  //     without ISM.
  nsresult rv = IMEStateManager::GetFocusSelectionAndRootElement(
      getter_AddRefs(mSelection), getter_AddRefs(mRootElement));
  if (rv != NS_ERROR_NOT_AVAILABLE) {
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    rv = Init(aEvent);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (RefPtr<EditContext> editContext = GetEditContext()) {
    editContext->DoSetSelection(*aEvent);
    aEvent->mSucceeded = true;
    return NS_OK;
  }

  // Get range from offset and length
  nsINode* startNode = nullptr;
  nsINode* endNode = nullptr;
  Maybe<uint32_t> startNodeOffset;
  Maybe<uint32_t> endNodeOffset;
  {
    Result<UnsafeDOMRangeAndAdjustedOffsetInFlattenedText, nsresult>
        domRangeAndAdjustedOffsetOrError =
            ConvertFlatTextOffsetToUnsafeDOMRange(
                aEvent->mOffset, aEvent->mLength,
                aEvent->ShouldExpandToClusterBoundary());
    if (MOZ_UNLIKELY(domRangeAndAdjustedOffsetOrError.isErr())) {
      NS_WARNING(
          "ContentEventHandler::ConvertFlatTextOffsetToDOMRangeBase() failed");
      return domRangeAndAdjustedOffsetOrError.unwrapErr();
    }
    const UnsafeDOMRangeAndAdjustedOffsetInFlattenedText
        domRangeAndAdjustedOffset = domRangeAndAdjustedOffsetOrError.unwrap();
    startNode = domRangeAndAdjustedOffset.mRange.GetStartContainer();
    endNode = domRangeAndAdjustedOffset.mRange.GetEndContainer();
    startNodeOffset = Some(domRangeAndAdjustedOffset.mRange.StartOffset());
    endNodeOffset = Some(domRangeAndAdjustedOffset.mRange.EndOffset());
    AdjustRangeForSelection(mRootElement, &startNode, &startNodeOffset);
    AdjustRangeForSelection(mRootElement, &endNode, &endNodeOffset);
    if (NS_WARN_IF(!startNode) || NS_WARN_IF(!endNode) ||
        NS_WARN_IF(startNodeOffset.isNothing()) ||
        NS_WARN_IF(endNodeOffset.isNothing())) {
      return NS_ERROR_UNEXPECTED;
    }
  }

  if (aEvent->IsReversed()) {
    nsCOMPtr<nsINode> startNodeStrong(startNode);
    nsCOMPtr<nsINode> endNodeStrong(endNode);
    ErrorResult error;
    MOZ_KnownLive(mSelection)
        ->SetBaseAndExtentInLimiter(*endNodeStrong, *endNodeOffset,
                                    *startNodeStrong, *startNodeOffset, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
  } else {
    nsCOMPtr<nsINode> startNodeStrong(startNode);
    nsCOMPtr<nsINode> endNodeStrong(endNode);
    ErrorResult error;
    MOZ_KnownLive(mSelection)
        ->SetBaseAndExtentInLimiter(*startNodeStrong, *startNodeOffset,
                                    *endNodeStrong, *endNodeOffset, error);
    if (NS_WARN_IF(error.Failed())) {
      return error.StealNSResult();
    }
  }

  // `ContentEventHandler` is a `MOZ_STACK_CLASS`, so `mSelection` is known to
  // be alive.
  MOZ_KnownLive(mSelection)
      ->ScrollIntoView(nsISelectionController::SELECTION_FOCUS_REGION);
  aEvent->mSucceeded = true;
  return NS_OK;
}

nsRect ContentEventHandler::FrameRelativeRect::RectRelativeTo(
    nsIFrame* aDestFrame) const {
  if (!mBaseFrame || NS_WARN_IF(!aDestFrame)) {
    return nsRect();
  }

  if (NS_WARN_IF(aDestFrame->PresContext() != mBaseFrame->PresContext())) {
    return nsRect();
  }

  if (aDestFrame == mBaseFrame) {
    return mRect;
  }

  nsIFrame* rootFrame = mBaseFrame->PresShell()->GetRootFrame();
  nsRect baseFrameRectInRootFrame = nsLayoutUtils::TransformFrameRectToAncestor(
      mBaseFrame, nsRect(), rootFrame);
  nsRect destFrameRectInRootFrame = nsLayoutUtils::TransformFrameRectToAncestor(
      aDestFrame, nsRect(), rootFrame);
  nsPoint difference =
      destFrameRectInRootFrame.TopLeft() - baseFrameRectInRootFrame.TopLeft();
  return mRect - difference;
}

}  // namespace mozilla
