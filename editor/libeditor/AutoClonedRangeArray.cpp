/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AutoClonedRangeArray.h"

#include "EditAction.h"
#include "EditorDOMPoint.h"   // for EditorDOMPoint, EditorDOMRange, etc
#include "EditorForwards.h"   // for CollectChildrenOptions
#include "HTMLEditUtils.h"    // for HTMLEditUtils
#include "HTMLEditHelpers.h"  // for SplitNodeResult
#include "TextEditor.h"       // for TextEditor
#include "WSRunScanner.h"     // for WSRunScanner

#include "mozilla/CaretAssociationHint.h"     // for CaretAssociationHint
#include "mozilla/IntegerRange.h"             // for IntegerRange
#include "mozilla/OwningNonNull.h"            // for OwningNonNull
#include "mozilla/PresShell.h"                // for PresShell
#include "mozilla/StaticPrefs_editor.h"       // for StaticPrefs::editor_*
#include "mozilla/dom/CharacterDataBuffer.h"  // for CharacterDataBuffer
#include "mozilla/dom/Document.h"             // for dom::Document
#include "mozilla/dom/EditContext.h"          // for dom::EditContext
#include "mozilla/dom/HTMLBRElement.h"        // for dom HTMLBRElement
#include "mozilla/dom/Selection.h"            // for dom::Selection
#include "mozilla/dom/Text.h"                 // for dom::Text

#include "gfxFontUtils.h"      // for gfxFontUtils
#include "nsError.h"           // for NS_SUCCESS_* and NS_ERROR_*
#include "nsFrameSelection.h"  // for nsFrameSelection
#include "nsIContent.h"        // for nsIContent
#include "nsINode.h"           // for nsINode

namespace mozilla {

using namespace dom;

using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;
using LeafNodeOption = HTMLEditUtils::LeafNodeOption;
using ReplaceOrVoidElementOption = HTMLEditUtils::ReplaceOrVoidElementOption;

/******************************************************************************
 * mozilla::AutoClonedRangeArray
 *****************************************************************************/

template AutoClonedRangeArray::AutoClonedRangeArray(
    const EditorDOMRange& aRange);
template AutoClonedRangeArray::AutoClonedRangeArray(
    const EditorRawDOMRange& aRange);
template AutoClonedRangeArray::AutoClonedRangeArray(
    const EditorDOMPoint& aRange);
template AutoClonedRangeArray::AutoClonedRangeArray(
    const EditorRawDOMPoint& aRange);

AutoClonedRangeArray::AutoClonedRangeArray(const AutoClonedRangeArray& aOther)
    : mAnchorFocusRange(aOther.mAnchorFocusRange),
      mDirection(aOther.mDirection) {
  mRanges.SetCapacity(aOther.mRanges.Length());
  for (const OwningNonNull<dom::Range>& range : aOther.mRanges) {
    RefPtr<dom::Range> clonedRange = range->CloneRange();
    mRanges.AppendElement(std::move(clonedRange));
  }
  mAnchorFocusRange = aOther.mAnchorFocusRange;
}

template <typename PointType>
AutoClonedRangeArray::AutoClonedRangeArray(
    const EditorDOMRangeBase<PointType>& aRange) {
  MOZ_ASSERT(aRange.IsPositionedAndValid());
  RefPtr<dom::Range> range = aRange.CreateRange(IgnoreErrors());
  if (NS_WARN_IF(!range) || NS_WARN_IF(!range->IsPositioned())) {
    return;
  }
  mRanges.AppendElement(*range);
  mAnchorFocusRange = std::move(range);
}

template <typename PT, typename CT>
AutoClonedRangeArray::AutoClonedRangeArray(
    const EditorDOMPointBase<PT, CT>& aPoint) {
  MOZ_ASSERT(aPoint.IsSetAndValid());
  RefPtr<dom::Range> range = aPoint.CreateCollapsedRange(IgnoreErrors());
  if (NS_WARN_IF(!range) || NS_WARN_IF(!range->IsPositioned())) {
    return;
  }
  mRanges.AppendElement(*range);
  mAnchorFocusRange = std::move(range);
}

AutoClonedRangeArray::AutoClonedRangeArray(const dom::Range& aRange) {
  MOZ_ASSERT(aRange.IsPositioned());
  mRanges.AppendElement(aRange.CloneRange());
  mAnchorFocusRange = mRanges[0];
}

// static
bool AutoClonedRangeArray::IsEditableRange(const dom::AbstractRange& aRange,
                                           const Element& aEditingHost) {
  // TODO: Perhaps, we should check whether the start/end boundaries are
  //       first/last point of non-editable element.
  //       See https://github.com/w3c/editing/issues/283#issuecomment-788654850
  EditorRawDOMPoint atStart(aRange.StartRef());
  if (!atStart.IsInContentNode() || !HTMLEditUtils::IsSimplyEditableNode(
                                        *atStart.ContainerAs<nsIContent>())) {
    return false;
  }

  if (aRange.GetStartContainer() != aRange.GetEndContainer()) {
    EditorRawDOMPoint atEnd(aRange.EndRef());
    if (!atEnd.IsInContentNode() || !HTMLEditUtils::IsSimplyEditableNode(
                                        *atEnd.ContainerAs<nsIContent>())) {
      return false;
    }

    // Now, both start and end points are editable, but if they are in
    // different editing host, we cannot edit the range.
    if (atStart.ContainerAs<nsIContent>() != atEnd.ContainerAs<nsIContent>() &&
        atStart.ContainerAs<nsIContent>()->GetEditingHost() !=
            atEnd.ContainerAs<nsIContent>()->GetEditingHost()) {
      return false;
    }
  }

  // HTMLEditor does not support modifying outside `<body>` element for now.
  nsINode* commonAncestor = aRange.GetClosestCommonInclusiveAncestor();
  if (aEditingHost.HasFlag(ELEMENT_HAS_EDIT_CONTEXT)) {
    EditContext* editContext =
        nsGenericHTMLElement::FromNode(aEditingHost)->GetEditContext();
    MOZ_ASSERT(editContext);
    if (commonAncestor == &editContext->TextNode()) {
      return true;
    }
  }
  return commonAncestor && commonAncestor->IsContent() &&
         commonAncestor->IsInclusiveDescendantOf(&aEditingHost);
}

void AutoClonedRangeArray::EnsureOnlyEditableRanges(
    const Element& aEditingHost) {
  for (const size_t index : Reversed(IntegerRange(mRanges.Length()))) {
    const OwningNonNull<dom::Range>& range = mRanges[index];
    if (!AutoClonedRangeArray::IsEditableRange(range, aEditingHost)) {
      mRanges.RemoveElementAt(index);
      continue;
    }
    // Special handling for `inert` attribute. If anchor node is inert, the
    // range should be treated as not editable.
    nsIContent* anchorContent =
        mDirection == eDirNext
            ? nsIContent::FromNode(range->GetStartContainer())
            : nsIContent::FromNode(range->GetEndContainer());
    if (anchorContent && HTMLEditUtils::ContentIsInert(*anchorContent)) {
      mRanges.RemoveElementAt(index);
      continue;
    }
    // Additionally, if focus node is inert, the range should be collapsed to
    // anchor node.
    nsIContent* focusContent =
        mDirection == eDirNext
            ? nsIContent::FromNode(range->GetEndContainer())
            : nsIContent::FromNode(range->GetStartContainer());
    if (focusContent && focusContent != anchorContent &&
        HTMLEditUtils::ContentIsInert(*focusContent)) {
      range->Collapse(mDirection == eDirNext);
    }
  }
  mAnchorFocusRange = mRanges.IsEmpty() ? nullptr : mRanges.LastElement().get();
}

bool AutoClonedRangeArray::AdjustRangesNotInReplacedNorVoidElements(
    RangeInReplacedOrVoidElement aRangeInReplacedOrVoidElement,
    const dom::Element& aEditingHost) {
  bool adjusted = false;
  for (const size_t index : Reversed(IntegerRange(mRanges.Length()))) {
    const OwningNonNull<dom::Range>& range = mRanges[index];
    // If the range is in a replaced element or a void element, we should adjust
    // the range boundaries outside of the element.
    if (Element* const replacedOrVoidElementAtStart =
            HTMLEditUtils::GetInclusiveAncestorReplacedOrVoidElement(
                *range->StartRef().GetContainer()->AsContent(),
                ReplaceOrVoidElementOption::LookForReplacedOrVoidElement)) {
      adjusted = true;
      if (MOZ_UNLIKELY(!replacedOrVoidElementAtStart->IsInclusiveDescendantOf(
              &aEditingHost))) {
        mRanges.RemoveElementAt(index);
        continue;
      }
      nsIContent* const commonAncestorContent =
          nsIContent::FromNode(range->GetClosestCommonInclusiveAncestor());
      if (commonAncestorContent &&
          commonAncestorContent->IsInclusiveDescendantOf(
              replacedOrVoidElementAtStart)) {
        // If the range is completely in a replaced element or a void element,
        // let's treat that it's collapsed before the element or just delete the
        // range.
        if (aRangeInReplacedOrVoidElement ==
                RangeInReplacedOrVoidElement::Delete ||
            NS_WARN_IF(NS_FAILED(range->CollapseTo(
                RawRangeBoundary::FromChild(*replacedOrVoidElementAtStart)))) ||
            MOZ_UNLIKELY(
                !AutoClonedRangeArray::IsEditableRange(range, aEditingHost))) {
          mRanges.RemoveElementAt(index);
          continue;
        }
        adjusted = true;
      } else {
        // If the range does not end in the replaced element or the void
        // element, let's treat that the range starts after the element.
        if (NS_WARN_IF(NS_FAILED(range->SetStartAndEnd(
                RawRangeBoundary::After(*replacedOrVoidElementAtStart),
                range->EndRef()))) ||
            MOZ_UNLIKELY(
                !AutoClonedRangeArray::IsEditableRange(range, aEditingHost))) {
          mRanges.RemoveElementAt(index);
          continue;
        }
      }
    }
    if (!range->Collapsed() &&
        range->GetStartContainer() != range->GetEndContainer()) {
      if (Element* const replacedOrVoidElementAtEnd =
              HTMLEditUtils::GetInclusiveAncestorReplacedOrVoidElement(
                  *range->EndRef().GetContainer()->AsContent(),
                  ReplaceOrVoidElementOption::LookForReplacedOrVoidElement)) {
        MOZ_ASSERT(
            replacedOrVoidElementAtEnd->IsInclusiveDescendantOf(&aEditingHost));
        adjusted = true;
        // If the range ends in a replaced element or a void element, let's
        // treat that the range ends before the element.
        if (NS_WARN_IF(NS_FAILED(range->SetStartAndEnd(
                range->StartRef(),
                RawRangeBoundary::FromChild(*replacedOrVoidElementAtEnd)))) ||
            MOZ_UNLIKELY(
                !AutoClonedRangeArray::IsEditableRange(range, aEditingHost))) {
          mRanges.RemoveElementAt(index);
          continue;
        }
      }
    }
  }
  return adjusted;
}

void AutoClonedRangeArray::EnsureRangesInTextNode(const Text& aTextNode) {
  auto GetOffsetInTextNode = [&aTextNode](const nsINode* aNode,
                                          uint32_t aOffset) -> uint32_t {
    MOZ_DIAGNOSTIC_ASSERT(aNode);
    if (aNode == &aTextNode) {
      return aOffset;
    }
    const nsIContent* anonymousDivElement = aTextNode.GetParent();
    MOZ_DIAGNOSTIC_ASSERT(anonymousDivElement);
    MOZ_DIAGNOSTIC_ASSERT(anonymousDivElement->IsHTMLElement(nsGkAtoms::div));
    MOZ_DIAGNOSTIC_ASSERT(anonymousDivElement->GetFirstChild() == &aTextNode);
    if (aNode == anonymousDivElement && aOffset == 0u) {
      return 0u;  // Point before the text node so that use start of the text.
    }
    MOZ_DIAGNOSTIC_ASSERT(aNode->IsInclusiveDescendantOf(anonymousDivElement));
    // Point after the text node so that use end of the text.
    return aTextNode.TextDataLength();
  };
  for (const OwningNonNull<dom::Range>& range : mRanges) {
    if (MOZ_LIKELY(range->GetStartContainer() == &aTextNode &&
                   range->GetEndContainer() == &aTextNode)) {
      continue;
    }
    range->SetStartAndEnd(
        const_cast<Text*>(&aTextNode),
        GetOffsetInTextNode(range->GetStartContainer(), range->StartOffset()),
        const_cast<Text*>(&aTextNode),
        GetOffsetInTextNode(range->GetEndContainer(), range->EndOffset()));
  }

  if (MOZ_UNLIKELY(mRanges.Length() >= 2)) {
    // For avoiding to handle same things in same range, we should drop and
    // merge unnecessary ranges.  Note that the ranges never overlap
    // because selection ranges are not allowed it so that we need to check only
    // end offset vs start offset of next one.
    for (const size_t i : Reversed(IntegerRange(mRanges.Length() - 1u))) {
      MOZ_ASSERT(mRanges[i]->EndOffset() < mRanges[i + 1]->StartOffset());
      // XXX Should we delete collapsed range unless the index is 0?  Without
      //     Selection API, such situation cannot happen so that `TextEditor`
      //     may behave unexpectedly.
      if (MOZ_UNLIKELY(mRanges[i]->EndOffset() >=
                       mRanges[i + 1]->StartOffset())) {
        const uint32_t newEndOffset = mRanges[i + 1]->EndOffset();
        mRanges.RemoveElementAt(i + 1);
        if (MOZ_UNLIKELY(NS_WARN_IF(newEndOffset > mRanges[i]->EndOffset()))) {
          // So, this case shouldn't happen.
          mRanges[i]->SetStartAndEnd(
              const_cast<Text*>(&aTextNode), mRanges[i]->StartOffset(),
              const_cast<Text*>(&aTextNode), newEndOffset);
        }
      }
    }
  }
}

Result<bool, nsresult>
AutoClonedRangeArray::ShrinkRangesIfStartFromOrEndAfterAtomicContent(
    const HTMLEditor& aHTMLEditor, nsIEditor::EDirection aDirectionAndAmount,
    IfSelectingOnlyOneAtomicContent aIfSelectingOnlyOneAtomicContent) {
  if (IsCollapsed()) {
    return false;
  }

  switch (aDirectionAndAmount) {
    case nsIEditor::eNext:
    case nsIEditor::eNextWord:
    case nsIEditor::ePrevious:
    case nsIEditor::ePreviousWord:
      break;
    default:
      return false;
  }

  bool changed = false;
  for (const OwningNonNull<dom::Range>& range : mRanges) {
    MOZ_ASSERT(!range->IsInAnySelection(),
               "Changing range in selection may cause running script");
    Result<bool, nsresult> result =
        WSRunScanner::ShrinkRangeIfStartsFromOrEndsAfterAtomicContent(
            {// We need to treat non-editable node in the range as an atomic
             // content.
             WSRunScanner::Option::OnlyEditableNodes,
             // The range may contain an atomic content with empty inline
             // containers and/or comment nodes which should be treated as
             // invisible to delete content.  Therefore, we shouldn't shrink to
             // the only visible atomic content in such case so that we need to
             // stop scanning if the start boundary is followed by an empty
             // container or a comment.
             WSRunScanner::Option::StopAtAnyEmptyInlineContainers,
             WSRunScanner::Option::StopAtComment},
            range);
    if (result.isErr()) {
      NS_WARNING(
          "WSRunScanner::ShrinkRangeIfStartsFromOrEndsAfterAtomicContent() "
          "failed");
      return Err(result.inspectErr());
    }
    changed |= result.inspect();
  }

  if (mRanges.Length() == 1 && aIfSelectingOnlyOneAtomicContent ==
                                   IfSelectingOnlyOneAtomicContent::Collapse) {
    MOZ_ASSERT(mRanges[0].get() == mAnchorFocusRange.get());
    if (mAnchorFocusRange->GetStartContainer() ==
            mAnchorFocusRange->GetEndContainer() &&
        mAnchorFocusRange->GetChildAtStartOffset() &&
        mAnchorFocusRange->StartRef().GetNextSiblingOfChildAtOffset() ==
            mAnchorFocusRange->GetChildAtEndOffset()) {
      mAnchorFocusRange->Collapse(aDirectionAndAmount == nsIEditor::eNext ||
                                  aDirectionAndAmount == nsIEditor::eNextWord);
      changed = true;
    }
  }

  return changed;
}

// static
void AutoClonedRangeArray::
    UpdatePointsToSelectAllChildrenIfCollapsedInEmptyBlockElement(
        EditorDOMPoint& aStartPoint, EditorDOMPoint& aEndPoint,
        const Element& aEditingHost) {
  // FYI: This was moved from
  // https://searchfox.org/mozilla-central/rev/3419858c997f422e3e70020a46baae7f0ec6dacc/editor/libeditor/HTMLEditSubActionHandler.cpp#6743

  // MOOSE major hack:
  // The GetPointAtFirstContentOfLineOrParentBlockIfFirstContentOfBlock() and
  // GetPointAfterFollowingLineBreakOrAtFollowingBlock() don't really do the
  // right thing for collapsed ranges inside block elements that contain nothing
  // but a solo <br>.  It's easier/ to put a workaround here than to revamp
  // them.  :-(
  if (aStartPoint != aEndPoint) {
    return;
  }

  if (!aStartPoint.IsInContentNode()) {
    return;
  }

  // XXX Perhaps, this should be more careful.  This may not select only one
  //     node because this just check whether the block is empty or not,
  //     and may not select in non-editable block.  However, for inline
  //     editing host case, it's right to look for block element without
  //     editable state check.  Now, this method is used for preparation for
  //     other things.  So, cannot write test for this method behavior.
  //     So, perhaps, we should get rid of this method and each caller should
  //     handle its job better.
  Element* const maybeNonEditableBlockElement =
      HTMLEditUtils::GetInclusiveAncestorElement(
          *aStartPoint.ContainerAs<nsIContent>(),
          HTMLEditUtils::ClosestBlockElement,
          BlockInlineCheck::UseComputedDisplayStyle);
  if (!maybeNonEditableBlockElement) {
    return;
  }

  // Make sure we don't go higher than our root element in the content tree
  if (aEditingHost.IsInclusiveDescendantOf(maybeNonEditableBlockElement)) {
    return;
  }

  if (HTMLEditUtils::IsEmptyNode(
          *maybeNonEditableBlockElement,
          {EmptyCheckOption::TreatNonEditableContentAsInvisible})) {
    aStartPoint.Set(maybeNonEditableBlockElement, 0u);
    aEndPoint.SetToEndOf(maybeNonEditableBlockElement);
  }
}

/**
 * Get the point before the line containing aPointInLine.
 *
 * @return              If the line starts after a `<br>` element, returns next
 *                      sibling of the `<br>` element.
 *                      If the line is first line of a block, returns point of
 *                      the block.
 * NOTE: The result may be point of editing host.  I.e., the container may be
 *       outside of editing host.
 */
MOZ_NEVER_INLINE_DEBUG static EditorDOMPoint
GetPointAtFirstContentOfLineOrParentHTMLBlockIfFirstContentOfBlock(
    const EditorDOMPoint& aPointInLine, EditSubAction aEditSubAction,
    BlockInlineCheck aBlockInlineCheck, const Element& aAncestorLimiter) {
  // FYI: This was moved from
  // https://searchfox.org/mozilla-central/rev/3419858c997f422e3e70020a46baae7f0ec6dacc/editor/libeditor/HTMLEditSubActionHandler.cpp#6447

  if (NS_WARN_IF(!aPointInLine.IsSet())) {
    return EditorDOMPoint();
  }

  EditorDOMPoint point(aPointInLine);
  // Start scanning from the container node if aPoint is in a text node.
  // XXX Perhaps, IsInDataNode() must be expected.
  if (point.IsInTextNode()) {
    if (!point.GetContainer()->GetParentNode()) {
      // Okay, can't promote any further
      // XXX Why don't we return start of the text node?
      return point;
    }
    // If there is a preformatted linefeed in the text node, let's return
    // the point after it.
    EditorDOMPoint atLastPreformattedNewLine =
        HTMLEditUtils::GetPreviousPreformattedNewLineInTextNode<EditorDOMPoint>(
            point);
    if (atLastPreformattedNewLine.IsSet()) {
      return atLastPreformattedNewLine.NextPoint();
    }
    point.Set(point.GetContainer());
  }

  // Look back through any further inline nodes that aren't across a <br>
  // from us, and that are enclosed in the same block.
  // I.e., looking for start of current hard line.
  for (nsIContent* previousEditableContent =
           HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
               point,
               {LeafNodeOption::IgnoreNonEditableNode,
                LeafNodeOption::TreatChildBlockAsLeafNode},
               aBlockInlineCheck, &aAncestorLimiter);
       previousEditableContent && previousEditableContent->GetParentNode() &&
       (!previousEditableContent->IsHTMLElement(nsGkAtoms::br) ||
        // FIXME: We're scanning backward so that it does not make sense to
        // check the following thing continuously.
        HTMLEditUtils::IsBRElementFollowedByBlockBoundary(
            static_cast<HTMLBRElement&>(*previousEditableContent))) &&
       !HTMLEditUtils::IsBlockElement(*previousEditableContent,
                                      aBlockInlineCheck);
       previousEditableContent =
           HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
               point,
               {LeafNodeOption::IgnoreNonEditableNode,
                LeafNodeOption::TreatChildBlockAsLeafNode},
               aBlockInlineCheck, &aAncestorLimiter)) {
    EditorDOMPoint atLastPreformattedNewLine =
        HTMLEditUtils::GetPreviousPreformattedNewLineInTextNode<EditorDOMPoint>(
            EditorRawDOMPoint::AtEndOf(*previousEditableContent));
    if (atLastPreformattedNewLine.IsSet()) {
      return atLastPreformattedNewLine.NextPoint();
    }
    point.Set(previousEditableContent);
  }

  // Finding the real start for this point unless current line starts after
  // <br> element.  Look up the tree for as long as we are the first node in
  // the container (typically, start of nearest block ancestor), and as long
  // as we haven't hit the body node.
  for (nsIContent* nearContent =
           HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
               point,
               {LeafNodeOption::IgnoreNonEditableNode,
                LeafNodeOption::TreatChildBlockAsLeafNode},
               aBlockInlineCheck, &aAncestorLimiter);
       !nearContent && !point.IsContainerHTMLElement(nsGkAtoms::body) &&
       point.GetContainerParent();
       nearContent =
           HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
               point,
               {LeafNodeOption::IgnoreNonEditableNode,
                LeafNodeOption::TreatChildBlockAsLeafNode},
               aBlockInlineCheck, &aAncestorLimiter)) {
    // Don't keep looking up if we have found a blockquote element to act on
    // when we handle outdent.
    // XXX Sounds like this is hacky.  If possible, it should be check in
    //     outdent handler for consistency between edit sub-actions.
    //     We should check Chromium's behavior of outdent when Selection
    //     starts from `<blockquote>` and starts from first child of
    //     `<blockquote>`.
    if (aEditSubAction == EditSubAction::eOutdent &&
        point.IsContainerHTMLElement(nsGkAtoms::blockquote)) {
      break;
    }

    // Don't walk past the editable section. Note that we need to check
    // before walking up to a parent because we need to return the parent
    // object, so the parent itself might not be in the editable area, but
    // it's OK if we're not performing a block-level action.
    bool blockLevelAction =
        aEditSubAction == EditSubAction::eIndent ||
        aEditSubAction == EditSubAction::eOutdent ||
        aEditSubAction == EditSubAction::eSetOrClearAlignment ||
        aEditSubAction == EditSubAction::eCreateOrRemoveBlock ||
        aEditSubAction == EditSubAction::eFormatBlockForHTMLCommand;
    // XXX So, does this check whether the container is removable or not? It
    //     seems that here can be rewritten as obviously what here tries to
    //     check.
    if (!point.GetContainerParent()->IsInclusiveDescendantOf(
            &aAncestorLimiter) &&
        (blockLevelAction ||
         !point.GetContainer()->IsInclusiveDescendantOf(&aAncestorLimiter))) {
      break;
    }

    // If we're formatting a block, we should reformat first ancestor format
    // block.
    if (aEditSubAction == EditSubAction::eFormatBlockForHTMLCommand &&
        point.IsContainerElement() &&
        HTMLEditUtils::IsFormatElementForFormatBlockCommand(
            *point.ContainerAs<Element>())) {
      point.Set(point.GetContainer());
      break;
    }

    point.Set(point.GetContainer());
  }
  return point;
}

/**
 * Get the point after the following line break or the block which breaks the
 * line containing aPointInLine.
 *
 * @return              If the line ends with a visible `<br>` element, returns
 *                      the point after the `<br>` element.
 *                      If the line ends with a preformatted linefeed, returns
 *                      the point after the linefeed unless it's an invisible
 *                      line break immediately before a block boundary.
 *                      If the line ends with a block boundary, returns the
 *                      point of the block.
 */
MOZ_NEVER_INLINE_DEBUG static EditorDOMPoint
GetPointAfterFollowingLineBreakOrAtFollowingHTMLBlock(
    const EditorDOMPoint& aPointInLine, EditSubAction aEditSubAction,
    BlockInlineCheck aBlockInlineCheck, const Element& aAncestorLimiter) {
  // FYI: This was moved from
  // https://searchfox.org/mozilla-central/rev/3419858c997f422e3e70020a46baae7f0ec6dacc/editor/libeditor/HTMLEditSubActionHandler.cpp#6541

  if (NS_WARN_IF(!aPointInLine.IsSet())) {
    return EditorDOMPoint();
  }

  EditorDOMPoint point(aPointInLine);
  // Start scanning from the container node if aPoint is in a text node.
  // XXX Perhaps, IsInDataNode() must be expected.
  if (point.IsInTextNode()) {
    if (NS_WARN_IF(!point.GetContainer()->GetParentNode())) {
      // Okay, can't promote any further
      // XXX Why don't we return end of the text node?
      return point;
    }
    EditorDOMPoint atNextPreformattedNewLine =
        HTMLEditUtils::GetInclusiveNextPreformattedNewLineInTextNode<
            EditorDOMPoint>(point);
    if (atNextPreformattedNewLine.IsSet()) {
      // If the linefeed is last character of the text node, it may be
      // invisible if it's immediately before a block boundary.  In such
      // case, we should return the block boundary.
      Element* maybeNonEditableBlockElement = nullptr;
      if (HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
              atNextPreformattedNewLine,
              HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes, nullptr,
              &maybeNonEditableBlockElement) &&
          maybeNonEditableBlockElement) {
        // If the block is a parent of the editing host, let's return end
        // of editing host.
        if (maybeNonEditableBlockElement == &aAncestorLimiter ||
            !maybeNonEditableBlockElement->IsInclusiveDescendantOf(
                &aAncestorLimiter)) {
          return EditorDOMPoint::AtEndOf(aAncestorLimiter);
        }
        // If it's invisible because of parent block boundary, return end
        // of the block.  Otherwise, i.e., it's followed by a child block,
        // returns the point of the child block.
        if (atNextPreformattedNewLine.ContainerAs<Text>()
                ->IsInclusiveDescendantOf(maybeNonEditableBlockElement)) {
          return EditorDOMPoint::AtEndOf(*maybeNonEditableBlockElement);
        }
        return EditorDOMPoint(maybeNonEditableBlockElement);
      }
      // Otherwise, return the point after the preformatted linefeed.
      return atNextPreformattedNewLine.NextPoint();
    }
    // want to be after the text node
    point.SetAfter(point.GetContainer());
    NS_WARNING_ASSERTION(point.IsSet(), "Failed to set to after the text node");
  }

  // Look ahead through any further inline nodes that aren't across a <br> from
  // us, and that are enclosed in the same block.
  // XXX Currently, we stop block-extending when finding visible <br> element.
  //     This might be different from "block-extend" of execCommand spec.
  //     However, the spec is really unclear.
  // XXX Probably, scanning only editable nodes is wrong for
  //     EditSubAction::eCreateOrRemoveBlock and
  //     EditSubAction::eFormatBlockForHTMLCommand because it might be better to
  //     wrap existing inline elements even if it's non-editable.  For example,
  //     following examples with insertParagraph causes different result:
  //     * <div contenteditable>foo[]<b contenteditable="false">bar</b></div>
  //     * <div contenteditable>foo[]<b>bar</b></div>
  //     * <div contenteditable>foo[]<b contenteditable="false">bar</b>baz</div>
  //     Only in the first case, after the caret position isn't wrapped with
  //     new <div> element.
  for (nsIContent* nextEditableContent =
           HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
               point,
               {LeafNodeOption::IgnoreNonEditableNode,
                LeafNodeOption::TreatChildBlockAsLeafNode},
               aBlockInlineCheck, &aAncestorLimiter);
       nextEditableContent &&
       !HTMLEditUtils::IsBlockElement(*nextEditableContent,
                                      aBlockInlineCheck) &&
       nextEditableContent->GetParent();
       nextEditableContent =
           HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
               point,
               {LeafNodeOption::IgnoreNonEditableNode,
                LeafNodeOption::TreatChildBlockAsLeafNode},
               aBlockInlineCheck, &aAncestorLimiter)) {
    EditorDOMPoint atFirstPreformattedNewLine =
        HTMLEditUtils::GetInclusiveNextPreformattedNewLineInTextNode<
            EditorDOMPoint>(EditorRawDOMPoint(nextEditableContent, 0));
    if (atFirstPreformattedNewLine.IsSet()) {
      // If the linefeed is last character of the text node, it may be
      // invisible if it's immediately before a block boundary.  In such
      // case, we should return the block boundary.
      Element* maybeNonEditableBlockElement = nullptr;
      if (HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
              atFirstPreformattedNewLine,
              HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes, nullptr,
              &maybeNonEditableBlockElement) &&
          maybeNonEditableBlockElement) {
        // If the block is a parent of the editing host, let's return end
        // of editing host.
        if (maybeNonEditableBlockElement == &aAncestorLimiter ||
            !maybeNonEditableBlockElement->IsInclusiveDescendantOf(
                &aAncestorLimiter)) {
          return EditorDOMPoint::AtEndOf(aAncestorLimiter);
        }
        // If it's invisible because of parent block boundary, return end
        // of the block.  Otherwise, i.e., it's followed by a child block,
        // returns the point of the child block.
        if (atFirstPreformattedNewLine.ContainerAs<Text>()
                ->IsInclusiveDescendantOf(maybeNonEditableBlockElement)) {
          return EditorDOMPoint::AtEndOf(*maybeNonEditableBlockElement);
        }
        return EditorDOMPoint(maybeNonEditableBlockElement);
      }
      // Otherwise, return the point after the preformatted linefeed.
      return atFirstPreformattedNewLine.NextPoint();
    }
    point.SetAfter(nextEditableContent);
    if (NS_WARN_IF(!point.IsSet())) {
      break;
    }
    if (HTMLBRElement* const nextBRElement =
            HTMLBRElement::FromNode(*nextEditableContent)) {
      if (!HTMLEditUtils::IsBRElementFollowedByBlockBoundary(*nextBRElement)) {
        break;
      }
    }
  }

  // Finding the real end for this point unless current line ends with a <br>
  // element.  Look up the tree for as long as we are the last node in the
  // container (typically, block node), and as long as we haven't hit the body
  // node.
  for (nsIContent* nearContent =
           HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
               point,
               {LeafNodeOption::IgnoreNonEditableNode,
                LeafNodeOption::TreatChildBlockAsLeafNode},
               aBlockInlineCheck, &aAncestorLimiter);
       !nearContent && !point.IsContainerHTMLElement(nsGkAtoms::body) &&
       point.GetContainerParent();
       nearContent = HTMLEditUtils::GetNextLeafContentOrNextBlockElement(
           point,
           {LeafNodeOption::IgnoreNonEditableNode,
            LeafNodeOption::TreatChildBlockAsLeafNode},
           aBlockInlineCheck, &aAncestorLimiter)) {
    // Don't walk past the editable section. Note that we need to check before
    // walking up to a parent because we need to return the parent object, so
    // the parent itself might not be in the editable area, but it's OK.
    // XXX Maybe returning parent of editing host is really error prone since
    //     everybody need to check whether the end point is in editing host
    //     when they touch there.
    if (!point.GetContainer()->IsInclusiveDescendantOf(&aAncestorLimiter) &&
        !point.GetContainerParent()->IsInclusiveDescendantOf(
            &aAncestorLimiter)) {
      break;
    }

    // If we're formatting a block, we should reformat first ancestor format
    // block.
    if (aEditSubAction == EditSubAction::eFormatBlockForHTMLCommand &&
        point.IsContainerElement() &&
        HTMLEditUtils::IsFormatElementForFormatBlockCommand(
            *point.ContainerAs<Element>())) {
      point.SetAfter(point.GetContainer());
      break;
    }

    point.SetAfter(point.GetContainer());
    if (NS_WARN_IF(!point.IsSet())) {
      break;
    }
  }
  return point;
}

void AutoClonedRangeArray::ExtendRangesToWrapLines(
    EditSubAction aEditSubAction, BlockInlineCheck aBlockInlineCheck,
    const Element& aAncestorLimiter) {
  // FYI: This is originated in
  // https://searchfox.org/mozilla-central/rev/1739f1301d658c9bff544a0a095ab11fca2e549d/editor/libeditor/HTMLEditSubActionHandler.cpp#6712

  bool removeSomeRanges = false;
  for (const OwningNonNull<dom::Range>& range : mRanges) {
    // Remove non-positioned ranges.
    if (MOZ_UNLIKELY(!range->IsPositioned())) {
      removeSomeRanges = true;
      continue;
    }
    // If the range is native anonymous subtrees, we must meet a bug of
    // `Selection` so that we need to hack here.
    if (MOZ_UNLIKELY(range->GetStartContainer()->IsInNativeAnonymousSubtree() ||
                     range->GetEndContainer()->IsInNativeAnonymousSubtree())) {
      EditorRawDOMRange rawRange(range);
      if (!rawRange.EnsureNotInNativeAnonymousSubtree()) {
        range->Reset();
        removeSomeRanges = true;
        continue;
      }
      if (NS_FAILED(
              range->SetStartAndEnd(rawRange.StartRef().ToRawRangeBoundary(),
                                    rawRange.EndRef().ToRawRangeBoundary())) ||
          MOZ_UNLIKELY(!range->IsPositioned())) {
        range->Reset();
        removeSomeRanges = true;
        continue;
      }
    }
    // Finally, extend the range.
    if (NS_FAILED(ExtendRangeToWrapStartAndEndLinesContainingBoundaries(
            range, aEditSubAction, aBlockInlineCheck, aAncestorLimiter))) {
      // If we failed to extend the range, we should use the original range
      // as-is unless the range is broken at setting the range.
      if (NS_WARN_IF(!range->IsPositioned())) {
        removeSomeRanges = true;
      }
    }
  }
  if (removeSomeRanges) {
    for (const size_t i : Reversed(IntegerRange(mRanges.Length()))) {
      if (!mRanges[i]->IsPositioned()) {
        mRanges.RemoveElementAt(i);
      }
    }
    if (!mAnchorFocusRange || !mAnchorFocusRange->IsPositioned()) {
      if (mRanges.IsEmpty()) {
        mAnchorFocusRange = nullptr;
      } else {
        mAnchorFocusRange = mRanges.LastElement();
      }
    }
  }
}

// static
nsresult
AutoClonedRangeArray::ExtendRangeToWrapStartAndEndLinesContainingBoundaries(
    dom::Range& aRange, EditSubAction aEditSubAction,
    BlockInlineCheck aBlockInlineCheck, const Element& aEditingHost) {
  MOZ_DIAGNOSTIC_ASSERT(
      !EditorRawDOMPoint(aRange.StartRef()).IsInNativeAnonymousSubtree());
  MOZ_DIAGNOSTIC_ASSERT(
      !EditorRawDOMPoint(aRange.EndRef()).IsInNativeAnonymousSubtree());

  if (NS_WARN_IF(!aRange.IsPositioned())) {
    return NS_ERROR_INVALID_ARG;
  }

  EditorDOMPoint startPoint(aRange.StartRef()), endPoint(aRange.EndRef());

  // If we're joining blocks, we call this for selecting a line to move.
  // Therefore, we don't want to select the ancestor blocks in this case
  // even if they are empty.
  if (aEditSubAction != EditSubAction::eMergeBlockContents) {
    AutoClonedRangeArray::
        UpdatePointsToSelectAllChildrenIfCollapsedInEmptyBlockElement(
            startPoint, endPoint, aEditingHost);
  }

  // Make a new adjusted range to represent the appropriate block content.
  // This is tricky.  The basic idea is to push out the range endpoints to
  // truly enclose the blocks that we will affect.

  // Make sure that the new range ends up to be in the editable section.
  // XXX Looks like that this check wastes the time.  Perhaps, we should
  //     implement a method which checks both two DOM points in the editor
  //     root.

  startPoint =
      GetPointAtFirstContentOfLineOrParentHTMLBlockIfFirstContentOfBlock(
          startPoint, aEditSubAction, aBlockInlineCheck, aEditingHost);
  // XXX GetPointAtFirstContentOfLineOrParentBlockIfFirstContentOfBlock() may
  //     return point of editing host.  Perhaps, we should change it and stop
  //     checking it here since this check may be expensive.
  // XXX If the container is an element in the editing host but it points end of
  //     the container, this returns nullptr.  Is it intentional?
  if (!startPoint.GetChildOrContainerIfDataNode() ||
      !startPoint.GetChildOrContainerIfDataNode()->IsInclusiveDescendantOf(
          &aEditingHost)) {
    return NS_ERROR_FAILURE;
  }
  endPoint = GetPointAfterFollowingLineBreakOrAtFollowingHTMLBlock(
      endPoint, aEditSubAction, aBlockInlineCheck, aEditingHost);
  const EditorDOMPoint lastRawPoint =
      endPoint.IsStartOfContainer() ? endPoint : endPoint.PreviousPoint();
  // XXX GetPointAfterFollowingLineBreakOrAtFollowingBlock() may return point of
  //     editing host.  Perhaps, we should change it and stop checking it here
  //     since this check may be expensive.
  // XXX If the container is an element in the editing host but it points end of
  //     the container, this returns nullptr.  Is it intentional?
  if (!lastRawPoint.GetChildOrContainerIfDataNode() ||
      !lastRawPoint.GetChildOrContainerIfDataNode()->IsInclusiveDescendantOf(
          &aEditingHost)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = aRange.SetStartAndEnd(startPoint.ToRawRangeBoundary(),
                                      endPoint.ToRawRangeBoundary());
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

Result<EditorDOMPoint, nsresult> AutoClonedRangeArray::
    SplitTextAtEndBoundariesAndInlineAncestorsAtBothBoundaries(
        HTMLEditor& aHTMLEditor, BlockInlineCheck aBlockInlineCheck,
        const Element& aEditingHost,
        const nsIContent* aAncestorLimiter /* = nullptr */) {
  // FYI: The following code is originated in
  // https://searchfox.org/mozilla-central/rev/c8e15e17bc6fd28f558c395c948a6251b38774ff/editor/libeditor/HTMLEditSubActionHandler.cpp#6971

  // Split text nodes. This is necessary, since given ranges may end in text
  // nodes in case where part of a pre-formatted elements needs to be moved.
  EditorDOMPoint pointToPutCaret;
  IgnoredErrorResult ignoredError;
  for (const OwningNonNull<dom::Range>& range : mRanges) {
    EditorDOMPoint atEnd(range->EndRef());
    if (NS_WARN_IF(!atEnd.IsSet()) || !atEnd.IsInTextNode() ||
        atEnd.GetContainer() == aAncestorLimiter) {
      continue;
    }

    if (!atEnd.IsStartOfContainer() && !atEnd.IsEndOfContainer()) {
      // Split the text node.
      Result<SplitNodeResult, nsresult> splitAtEndResult =
          aHTMLEditor.SplitNodeWithTransaction(atEnd);
      if (MOZ_UNLIKELY(splitAtEndResult.isErr())) {
        NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
        return splitAtEndResult.propagateErr();
      }
      SplitNodeResult unwrappedSplitAtEndResult = splitAtEndResult.unwrap();
      unwrappedSplitAtEndResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});

      // Correct the range.
      // The new end parent becomes the parent node of the text.
      MOZ_ASSERT(!range->IsInAnySelection());
      range->SetEnd(unwrappedSplitAtEndResult.AtNextContent<EditorRawDOMPoint>()
                        .ToRawRangeBoundary(),
                    ignoredError);
      NS_WARNING_ASSERTION(!ignoredError.Failed(),
                           "Range::SetEnd() failed, but ignored");
      ignoredError.SuppressException();
    }
  }

  // FYI: The following code is originated in
  // https://searchfox.org/mozilla-central/rev/c8e15e17bc6fd28f558c395c948a6251b38774ff/editor/libeditor/HTMLEditSubActionHandler.cpp#7023
  AutoTArray<OwningNonNull<RangeItem>, 8> rangeItemArray;
  rangeItemArray.AppendElements(mRanges.Length());

  // First register ranges for special editor gravity
  Maybe<size_t> anchorFocusRangeIndex;
  for (const size_t index : IntegerRange(rangeItemArray.Length())) {
    rangeItemArray[index] = new RangeItem();
    rangeItemArray[index]->StoreRange(*mRanges[index]);
    aHTMLEditor.RangeUpdaterRef().RegisterRangeItem(*rangeItemArray[index]);
    if (mRanges[index] == mAnchorFocusRange) {
      anchorFocusRangeIndex = Some(index);
    }
  }
  // TODO: We should keep the array, and just update the ranges.
  mRanges.Clear();
  mAnchorFocusRange = nullptr;
  // Now bust up inlines.
  nsresult rv = NS_OK;
  for (const OwningNonNull<RangeItem>& item : Reversed(rangeItemArray)) {
    // MOZ_KnownLive because 'rangeItemArray' is guaranteed to keep it alive.
    Result<EditorDOMPoint, nsresult> splitParentsResult =
        aHTMLEditor.SplitInlineAncestorsAtRangeBoundaries(
            MOZ_KnownLive(*item), aBlockInlineCheck, aEditingHost,
            aAncestorLimiter);
    if (MOZ_UNLIKELY(splitParentsResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitInlineAncestorsAtRangeBoundaries() failed");
      rv = splitParentsResult.unwrapErr();
      break;
    }
    if (splitParentsResult.inspect().IsSet()) {
      pointToPutCaret = splitParentsResult.unwrap();
    }
  }
  // Then unregister the ranges
  for (const size_t index : IntegerRange(rangeItemArray.Length())) {
    aHTMLEditor.RangeUpdaterRef().DropRangeItem(rangeItemArray[index]);
    RefPtr<dom::Range> range = rangeItemArray[index]->GetRange();
    if (range && range->IsPositioned()) {
      if (anchorFocusRangeIndex.isSome() && index == *anchorFocusRangeIndex) {
        mAnchorFocusRange = range;
      }
      mRanges.AppendElement(std::move(range));
    }
  }
  if (!mAnchorFocusRange && !mRanges.IsEmpty()) {
    mAnchorFocusRange = mRanges.LastElement();
  }

  // XXX Why do we ignore the other errors here??
  if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  return pointToPutCaret;
}

nsresult AutoClonedRangeArray::CollectEditTargetNodes(
    const HTMLEditor& aHTMLEditor,
    nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents,
    EditSubAction aEditSubAction,
    CollectNonEditableNodes aCollectNonEditableNodes) const {
  MOZ_ASSERT(aHTMLEditor.IsEditActionDataAvailable());

  // FYI: This was moved from
  // https://searchfox.org/mozilla-central/rev/4bce7d85ba4796dd03c5dcc7cfe8eee0e4c07b3b/editor/libeditor/HTMLEditSubActionHandler.cpp#7060

  // Gather up a list of all the nodes
  for (const OwningNonNull<dom::Range>& range : mRanges) {
    DOMSubtreeIterator iter;
    nsresult rv = iter.Init(*range);
    if (NS_FAILED(rv)) {
      NS_WARNING("DOMSubtreeIterator::Init() failed");
      return rv;
    }
    if (aOutArrayOfContents.IsEmpty()) {
      iter.AppendAllNodesToArray(aOutArrayOfContents);
    } else {
      AutoTArray<OwningNonNull<nsIContent>, 24> arrayOfTopChildren;
      iter.AppendNodesToArray(
          +[](nsINode& aNode, void* aArray) -> bool {
            MOZ_ASSERT(aArray);
            return !static_cast<nsTArray<OwningNonNull<nsIContent>>*>(aArray)
                        ->Contains(&aNode);
          },
          arrayOfTopChildren, &aOutArrayOfContents);
      aOutArrayOfContents.AppendElements(std::move(arrayOfTopChildren));
    }
    if (aCollectNonEditableNodes == CollectNonEditableNodes::No) {
      for (const size_t i :
           Reversed(IntegerRange(aOutArrayOfContents.Length()))) {
        if (!EditorUtils::IsEditableContent(aOutArrayOfContents[i],
                                            EditorUtils::EditorType::HTML)) {
          aOutArrayOfContents.RemoveElementAt(i);
        }
      }
    }
  }

  switch (aEditSubAction) {
    case EditSubAction::eCreateOrRemoveBlock:
    case EditSubAction::eFormatBlockForHTMLCommand: {
      // Certain operations should not act on li's and td's, but rather inside
      // them.  Alter the list as needed.
      CollectChildrenOptions options = {
          CollectChildrenOption::CollectListChildren,
          CollectChildrenOption::CollectTableChildren};
      if (aCollectNonEditableNodes == CollectNonEditableNodes::No) {
        options += CollectChildrenOption::IgnoreNonEditableChildren;
      }
      if (aEditSubAction == EditSubAction::eCreateOrRemoveBlock) {
        for (const size_t index :
             Reversed(IntegerRange(aOutArrayOfContents.Length()))) {
          const OwningNonNull<nsIContent> content = aOutArrayOfContents[index];
          if (HTMLEditUtils::IsListItemElement(*content)) {
            aOutArrayOfContents.RemoveElementAt(index);
            HTMLEditUtils::CollectChildren(*content, aOutArrayOfContents, index,
                                           options);
          }
        }
      } else {
        // <dd> and <dt> are format blocks.  Therefore, we should not handle
        // their children directly.  They should be replaced with new format
        // block.
        MOZ_ASSERT(
            HTMLEditUtils::IsFormatTagForFormatBlockCommand(*nsGkAtoms::dt));
        MOZ_ASSERT(
            HTMLEditUtils::IsFormatTagForFormatBlockCommand(*nsGkAtoms::dd));
        for (const size_t index :
             Reversed(IntegerRange(aOutArrayOfContents.Length()))) {
          const OwningNonNull<nsIContent> content = aOutArrayOfContents[index];
          MOZ_ASSERT_IF(HTMLEditUtils::IsListItemElement(*content),
                        content->IsAnyOfHTMLElements(
                            nsGkAtoms::dd, nsGkAtoms::dt, nsGkAtoms::li));
          if (content->IsHTMLElement(nsGkAtoms::li)) {
            aOutArrayOfContents.RemoveElementAt(index);
            HTMLEditUtils::CollectChildren(*content, aOutArrayOfContents, index,
                                           options);
          }
        }
      }
      // Empty text node shouldn't be selected if unnecessary
      for (const size_t index :
           Reversed(IntegerRange(aOutArrayOfContents.Length()))) {
        if (const Text* text = aOutArrayOfContents[index]->GetAsText()) {
          // Don't select empty text except to empty block
          if (!HTMLEditUtils::IsVisibleTextNode(
                  *text, HTMLEditUtils::TreatInvisibleLineBreakAs::Visible)) {
            aOutArrayOfContents.RemoveElementAt(index);
          }
        }
      }
      break;
    }
    case EditSubAction::eCreateOrChangeList: {
      // XXX aCollectNonEditableNodes is ignored here.  Maybe a bug.
      CollectChildrenOptions options = {
          CollectChildrenOption::CollectTableChildren};
      for (const size_t index :
           Reversed(IntegerRange(aOutArrayOfContents.Length()))) {
        // Scan for table elements.  If we find table elements other than
        // table, replace it with a list of any editable non-table content
        // because if a selection range starts from end in a table-cell and
        // ends at or starts from outside the `<table>`, we need to make
        // lists in each selected table-cells.
        const OwningNonNull<nsIContent> content = aOutArrayOfContents[index];
        if (HTMLEditUtils::IsAnyTableElementExceptTableElementAndColumElement(
                content)) {
          aOutArrayOfContents.RemoveElementAt(index);
          HTMLEditUtils::CollectChildren(content, aOutArrayOfContents, index,
                                         options);
        }
      }
      // If there is only one node in the array, and it is a `<div>`,
      // `<blockquote>` or a list element, then look inside of it until we
      // find inner list or content.
      if (aOutArrayOfContents.Length() != 1) {
        break;
      }
      Element* const deepestDivBlockquoteOrListElement =
          HTMLEditUtils::GetInclusiveDeepestFirstChildWhichHasOneChild(
              aOutArrayOfContents[0], {LeafNodeOption::IgnoreNonEditableNode},
              BlockInlineCheck::Auto, nsGkAtoms::div, nsGkAtoms::blockquote,
              nsGkAtoms::ul, nsGkAtoms::ol, nsGkAtoms::dl);
      if (!deepestDivBlockquoteOrListElement) {
        break;
      }
      if (deepestDivBlockquoteOrListElement->IsAnyOfHTMLElements(
              nsGkAtoms::div, nsGkAtoms::blockquote)) {
        aOutArrayOfContents.Clear();
        // XXX Before we're called, non-editable nodes are ignored.  However,
        //     we may append non-editable nodes here.
        HTMLEditUtils::CollectChildren(*deepestDivBlockquoteOrListElement,
                                       aOutArrayOfContents, 0, {});
        break;
      }
      aOutArrayOfContents.ReplaceElementAt(
          0, OwningNonNull<nsIContent>(*deepestDivBlockquoteOrListElement));
      break;
    }
    case EditSubAction::eOutdent:
    case EditSubAction::eIndent:
    case EditSubAction::eSetPositionToAbsolute: {
      // Indent/outdent already do something special for list items, but we
      // still need to make sure we don't act on table elements
      CollectChildrenOptions options = {
          CollectChildrenOption::CollectListChildren,
          CollectChildrenOption::CollectTableChildren};
      if (aCollectNonEditableNodes == CollectNonEditableNodes::No) {
        options += CollectChildrenOption::IgnoreNonEditableChildren;
      }
      for (const size_t index :
           Reversed(IntegerRange(aOutArrayOfContents.Length()))) {
        const OwningNonNull<nsIContent> content = aOutArrayOfContents[index];
        if (HTMLEditUtils::IsAnyTableElementExceptTableElementAndColumElement(
                content)) {
          aOutArrayOfContents.RemoveElementAt(index);
          HTMLEditUtils::CollectChildren(*content, aOutArrayOfContents, index,
                                         options);
        }
      }
      break;
    }
    default:
      break;
  }

  // Outdent should look inside of divs.
  if (aEditSubAction == EditSubAction::eOutdent &&
      !aHTMLEditor.IsCSSEnabled()) {
    CollectChildrenOptions options = {};
    if (aCollectNonEditableNodes == CollectNonEditableNodes::No) {
      options += CollectChildrenOption::IgnoreNonEditableChildren;
    }
    for (const size_t index :
         Reversed(IntegerRange(aOutArrayOfContents.Length()))) {
      OwningNonNull<nsIContent> content = aOutArrayOfContents[index];
      if (content->IsHTMLElement(nsGkAtoms::div)) {
        aOutArrayOfContents.RemoveElementAt(index);
        HTMLEditUtils::CollectChildren(*content, aOutArrayOfContents, index,
                                       options);
      }
    }
  }

  return NS_OK;
}

Element* AutoClonedRangeArray::GetClosestAncestorAnyListElementOfRange() const {
  for (const OwningNonNull<dom::Range>& range : mRanges) {
    nsINode* commonAncestorNode = range->GetClosestCommonInclusiveAncestor();
    if (MOZ_UNLIKELY(!commonAncestorNode)) {
      continue;
    }
    for (Element* const element :
         commonAncestorNode->InclusiveAncestorsOfType<Element>()) {
      if (HTMLEditUtils::IsListElement(*element)) {
        return element;
      }
    }
  }
  return nullptr;
}

void AutoClonedRangeArray::RemoveCollapsedRanges(
    PreservePrimaryRangeIfAllRangesCollapsed aPreservePrimaryRange) {
  for (const auto index : Reversed(IntegerRange(mRanges.Length()))) {
    if (mRanges[index]->Collapsed()) {
      mRanges.RemoveElementAt(index);
    }
  }
  if (mAnchorFocusRange->Collapsed()) {
    MOZ_ASSERT(!mRanges.Contains(mAnchorFocusRange.get()));
    if (mRanges.IsEmpty()) {
      if (aPreservePrimaryRange ==
          PreservePrimaryRangeIfAllRangesCollapsed::Yes) {
        RemoveAllRanges();
      } else {
        mRanges.AppendElement(*mAnchorFocusRange);
      }
    } else {
      mAnchorFocusRange = mRanges.LastElement();
    }
  } else {
    MOZ_ASSERT(mRanges.Contains(mAnchorFocusRange.get()));
  }
}

void AutoClonedRangeArray::ExtendRangeToContainSurroundingInvisibleWhiteSpaces(
    nsIEditor::EStripWrappers aStripWrappers) {
  const auto PointAfterLineBoundary =
      [](const WSScanResult& aPreviousThing) -> EditorRawDOMPoint {
    if (aPreviousThing.ReachedCurrentBlockBoundary()) {
      return EditorRawDOMPoint(aPreviousThing.ElementPtr(), 0u);
    }
    return aPreviousThing.PointAfterReachedContent<EditorRawDOMPoint>();
  };
  const auto PointeAtLineBoundary =
      [](const WSScanResult& aNextThing) -> EditorRawDOMPoint {
    if (aNextThing.ReachedCurrentBlockBoundary()) {
      return EditorRawDOMPoint::AtEndOf(*aNextThing.ElementPtr());
    }
    return aNextThing.PointAtReachedContent<EditorRawDOMPoint>();
  };
  for (const OwningNonNull<dom::Range>& range : mRanges) {
    if (MOZ_UNLIKELY(range->Collapsed())) {
      // Don't extend the collapsed range to do nothing for the range.
      continue;
    }
    const WSScanResult previousThing =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes},
            EditorRawDOMPoint(range->StartRef()));
    if (previousThing.ReachedLineBoundary()) {
      const EditorRawDOMPoint mostDistantNewStart =
          [&]() MOZ_NEVER_INLINE_DEBUG {
            if (aStripWrappers == nsIEditor::eStrip) {
              nsINode* const commonAncestor =
                  range->GetClosestCommonInclusiveAncestor();
              MOZ_ASSERT(commonAncestor);
              Element* const commonContainer =
                  commonAncestor->GetAsElementOrParentElement();
              if (NS_WARN_IF(!commonContainer)) {
                return EditorRawDOMPoint();
              }
              return EditorRawDOMPoint(commonContainer, 0u);
            }
            Element* const container =
                range->StartRef().GetContainer()->GetAsElementOrParentElement();
            if (NS_WARN_IF(!container)) {
              return EditorRawDOMPoint();
            }
            return EditorRawDOMPoint(container, 0u);
          }();
      const EditorRawDOMPoint afterLineBoundary =
          PointAfterLineBoundary(previousThing);
      const auto& newStart =
          [&]() MOZ_NEVER_INLINE_DEBUG -> const EditorRawDOMPoint& {
        // If the container wraps the line boundary, we can extend the range
        // to the line boundary.
        if (MOZ_UNLIKELY(!mostDistantNewStart.IsSet()) ||
            mostDistantNewStart.IsBefore(afterLineBoundary)) {
          return afterLineBoundary;
        }
        // If the container does not wrap the line boundary, we can delete
        // first content of the container.
        return mostDistantNewStart;
      }();
      const auto betterNewStart = [&]() MOZ_NEVER_INLINE_DEBUG {
        if (MOZ_UNLIKELY(!newStart.IsSet())) {
          return EditorRawDOMPoint();
        }
        MOZ_ASSERT_IF(mostDistantNewStart.IsSet(),
                      mostDistantNewStart.IsStartOfContainer());
        auto* const firstText = Text::FromNodeOrNull(
            newStart == mostDistantNewStart
                ? mostDistantNewStart.GetContainer()->GetFirstChild()
                : newStart.GetChild());
        if (!firstText) {
          return newStart;
        }
        return EditorRawDOMPoint(firstText, 0u);
      }();
      if (MOZ_LIKELY(!NS_WARN_IF(!betterNewStart.IsSet())) &&
          betterNewStart != range->StartRef()) {
        IgnoredErrorResult ignoredError;
        range->SetStart(betterNewStart.ToRawRangeBoundary(), ignoredError);
        NS_WARNING_ASSERTION(!ignoredError.Failed(),
                             "Range::SetStart() failed, but ignored");
      }
    }
    const WSScanResult nextThing =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes},
            EditorRawDOMPoint(range->EndRef()));
    if (!nextThing.ReachedLineBoundary()) {
      continue;
    }
    const EditorRawDOMPoint mostDistantNewEnd = [&]() MOZ_NEVER_INLINE_DEBUG {
      if (aStripWrappers == nsIEditor::eStrip) {
        nsINode* const commonAncestor =
            range->GetClosestCommonInclusiveAncestor();
        MOZ_ASSERT(commonAncestor);
        Element* const commonContainer =
            commonAncestor->GetAsElementOrParentElement();
        if (NS_WARN_IF(!commonContainer)) {
          return EditorRawDOMPoint();
        }
        return EditorRawDOMPoint::AtEndOf(*commonContainer);
      }
      Element* const container =
          range->EndRef().GetContainer()->GetAsElementOrParentElement();
      if (NS_WARN_IF(!container)) {
        return EditorRawDOMPoint();
      }
      return EditorRawDOMPoint::AtEndOf(*container);
    }();
    if (MOZ_UNLIKELY(!mostDistantNewEnd.IsSet())) {
      continue;
    }
    const EditorRawDOMPoint atLineBoundary = PointeAtLineBoundary(nextThing);
    const auto& newEnd =
        [&]() MOZ_NEVER_INLINE_DEBUG -> const EditorRawDOMPoint& {
      // If the container wraps the line boundary, we can use the boundary
      // point.
      if (atLineBoundary.IsBefore(mostDistantNewEnd)) {
        return atLineBoundary;
      }
      // If the container does not wrap the line boundary, we can delete last
      // content of the container.
      return mostDistantNewEnd;
    }();
    if (MOZ_UNLIKELY(!newEnd.IsSet())) {
      continue;
    }
    const auto betterNewEnd = [&]() MOZ_NEVER_INLINE_DEBUG {
      MOZ_ASSERT_IF(mostDistantNewEnd.IsSet(),
                    mostDistantNewEnd.IsEndOfContainer());
      auto* const lastText = Text::FromNodeOrNull(
          newEnd == mostDistantNewEnd
              ? mostDistantNewEnd.GetContainer()->GetLastChild()
              : (!newEnd.IsStartOfContainer()
                     ? newEnd.GetPreviousSiblingOfChild()
                     : nullptr));
      if (!lastText) {
        return newEnd;
      }
      return EditorRawDOMPoint::AtEndOf(*lastText);
    }();
    if (NS_WARN_IF(!betterNewEnd.IsSet()) || betterNewEnd == range->EndRef()) {
      continue;
    }
    IgnoredErrorResult ignoredError;
    range->SetEnd(betterNewEnd.ToRawRangeBoundary(), ignoredError);
    NS_WARNING_ASSERTION(!ignoredError.Failed(),
                         "Range::SetEnd() failed, but ignored");
  }
}

/******************************************************************************
 * mozilla::AutoClonedSelectionRangeArray
 *****************************************************************************/

template AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const EditorDOMRange& aRange,
    const LimitersAndCaretData& aLimitersAndCaretData);
template AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const EditorRawDOMRange& aRange,
    const LimitersAndCaretData& aLimitersAndCaretData);
template AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const EditorDOMPoint& aRange,
    const LimitersAndCaretData& aLimitersAndCaretData);
template AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const EditorRawDOMPoint& aRange,
    const LimitersAndCaretData& aLimitersAndCaretData);

AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const dom::Selection& aSelection) {
  Initialize(aSelection);
}

AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const AutoClonedSelectionRangeArray& aOther)
    : AutoClonedRangeArray(aOther),
      mLimitersAndCaretData(aOther.mLimitersAndCaretData) {}

template <typename PointType>
AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const EditorDOMRangeBase<PointType>& aRange,
    const LimitersAndCaretData& aLimitersAndCaretData)
    : mLimitersAndCaretData(aLimitersAndCaretData) {
  MOZ_ASSERT(aRange.IsPositionedAndValid());
  RefPtr<dom::Range> range = aRange.CreateRange(IgnoreErrors());
  if (NS_WARN_IF(!range) || NS_WARN_IF(!range->IsPositioned()) ||
      NS_WARN_IF(!RangeIsInLimiters(*range))) {
    return;
  }
  mRanges.AppendElement(*range);
  mAnchorFocusRange = std::move(range);
}

template <typename PT, typename CT>
AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const EditorDOMPointBase<PT, CT>& aPoint,
    const LimitersAndCaretData& aLimitersAndCaretData)
    : mLimitersAndCaretData(aLimitersAndCaretData) {
  MOZ_ASSERT(aPoint.IsSetAndValid());
  if (NS_WARN_IF(!NodeIsInLimiters(aPoint.GetContainer()))) {
    return;
  }
  RefPtr<dom::Range> range = aPoint.CreateCollapsedRange(IgnoreErrors());
  if (NS_WARN_IF(!range) || NS_WARN_IF(!range->IsPositioned())) {
    return;
  }
  mRanges.AppendElement(*range);
  mAnchorFocusRange = std::move(range);
  SetNewCaretAssociationHint(aPoint.ToRawRangeBoundary(),
                             aPoint.GetInterlinePosition());
}

AutoClonedSelectionRangeArray::AutoClonedSelectionRangeArray(
    const dom::Range& aRange, const LimitersAndCaretData& aLimitersAndCaretData)
    : mLimitersAndCaretData(aLimitersAndCaretData) {
  MOZ_ASSERT(aRange.IsPositioned());
  if (NS_WARN_IF(!RangeIsInLimiters(aRange))) {
    return;
  }
  mRanges.AppendElement(aRange.CloneRange());
  mAnchorFocusRange = mRanges[0];
}

void AutoClonedSelectionRangeArray::SetNewCaretAssociationHint(
    const RawRangeBoundary& aRawRangeBoundary,
    InterlinePosition aInternlinePosition) {
  if (aInternlinePosition == Selection::InterlinePosition::Undefined) {
    mLimitersAndCaretData.mCaretAssociationHint = ComputeCaretAssociationHint(
        mLimitersAndCaretData.mCaretAssociationHint,
        mLimitersAndCaretData.mCaretBidiLevel, aRawRangeBoundary);
  } else {
    SetInterlinePosition(aInternlinePosition);
  }
}

bool AutoClonedSelectionRangeArray::SaveAndTrackRanges(
    HTMLEditor& aHTMLEditor) {
  if (mSavedRanges.isSome()) {
    return false;
  }
  mSavedRanges.emplace(*this);
  aHTMLEditor.RangeUpdaterRef().RegisterSelectionState(mSavedRanges.ref());
  mTrackingHTMLEditor = &aHTMLEditor;
  return true;
}

void AutoClonedSelectionRangeArray::ClearSavedRanges() {
  if (mSavedRanges.isNothing()) {
    return;
  }
  OwningNonNull<HTMLEditor> htmlEditor(std::move(mTrackingHTMLEditor));
  MOZ_ASSERT(!mTrackingHTMLEditor);
  htmlEditor->RangeUpdaterRef().DropSelectionState(mSavedRanges.ref());
  mSavedRanges.reset();
}

Result<nsIEditor::EDirection, nsresult>
AutoClonedSelectionRangeArray::ExtendAnchorFocusRangeFor(
    const EditorBase& aEditorBase, nsIEditor::EDirection aDirectionAndAmount) {
  MOZ_ASSERT(aEditorBase.IsEditActionDataAvailable());
  MOZ_ASSERT(mAnchorFocusRange);
  MOZ_ASSERT(mAnchorFocusRange->IsPositioned());
  MOZ_ASSERT(mAnchorFocusRange->StartRef().IsSet());
  MOZ_ASSERT(mAnchorFocusRange->EndRef().IsSet());

  if (!EditorUtils::IsFrameSelectionRequiredToExtendSelection(
          aDirectionAndAmount, *this)) {
    return aDirectionAndAmount;
  }

  if (NS_WARN_IF(mRanges.IsEmpty())) {
    return Err(NS_ERROR_FAILURE);
  }

  const RefPtr<PresShell> presShell = aEditorBase.GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return Err(NS_ERROR_FAILURE);
  }

  const RefPtr<Element> editingHost =
      aEditorBase.IsHTMLEditor()
          ? aEditorBase.AsHTMLEditor()->ComputeEditingHost()
          : nullptr;
  if (aEditorBase.IsHTMLEditor() && NS_WARN_IF(!editingHost)) {
    return Err(NS_ERROR_FAILURE);
  }

  Result<RefPtr<dom::Range>, nsresult> result(NS_ERROR_UNEXPECTED);
  const OwningNonNull<dom::Range> anchorFocusRange = *mAnchorFocusRange;
  const LimitersAndCaretData limitersAndCaretData =
      [&]() -> LimitersAndCaretData {
    if (aEditorBase.IsHTMLEditor()) {
      nsIContent* const commonAncestorContent = nsIContent::FromNodeOrNull(
          anchorFocusRange->GetClosestCommonInclusiveAncestor());
      if (commonAncestorContent) {
        Element* const anyTableElement =
            HTMLEditUtils::GetInclusiveAncestorAnyTableElement(
                *commonAncestorContent, mLimitersAndCaretData.mAncestorLimiter);
        if (anyTableElement) {
          // We shouldn't extend anchorFocusRange with crossing new any table
          // element boundary.
          LimitersAndCaretData limitInTableElement = mLimitersAndCaretData;
          limitInTableElement.mAncestorLimiter = anyTableElement;
          return limitInTableElement;
        }
      }
    }
    return mLimitersAndCaretData;
  }();
  const nsDirection rangeDirection =
      mDirection == eDirNext ? eDirNext : eDirPrevious;
  nsIEditor::EDirection directionAndAmountResult = aDirectionAndAmount;
  switch (aDirectionAndAmount) {
    case nsIEditor::eNextWord:
      result =
          nsFrameSelection::CreateRangeExtendedToNextWordBoundary<dom::Range>(
              *presShell, limitersAndCaretData, anchorFocusRange,
              rangeDirection);
      if (NS_WARN_IF(aEditorBase.Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (result.isErr()) [[unlikely]] {
        // Couldn't extend the range to outside the limiter. Don't warn this
        // since the user may press Backspace or Delete at start or end of the
        // editing host or a table cell/caption element.
        return result.propagateErr();
      }
      // DeleteSelectionWithTransaction() doesn't handle these actions
      // because it's inside batching, so don't confuse it:
      directionAndAmountResult = nsIEditor::eNone;
      break;
    case nsIEditor::ePreviousWord:
      result = nsFrameSelection::CreateRangeExtendedToPreviousWordBoundary<
          dom::Range>(*presShell, limitersAndCaretData, anchorFocusRange,
                      rangeDirection);
      if (NS_WARN_IF(aEditorBase.Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (result.isErr()) [[unlikely]] {
        // Couldn't extend the range to outside the limiter. Don't warn this
        // since the user may press Backspace or Delete at start or end of the
        // editing host or a table cell/caption element.
        return result.propagateErr();
      }
      // DeleteSelectionWithTransaction() doesn't handle these actions
      // because it's inside batching, so don't confuse it:
      directionAndAmountResult = nsIEditor::eNone;
      break;
    case nsIEditor::eNext:
      result =
          nsFrameSelection::CreateRangeExtendedToNextGraphemeClusterBoundary<
              dom::Range>(*presShell, limitersAndCaretData, anchorFocusRange,
                          rangeDirection);
      if (NS_WARN_IF(aEditorBase.Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (result.isErr()) [[unlikely]] {
        // Couldn't extend the range to outside the limiter. Don't warn this
        // since the user may press Backspace or Delete at start or end of the
        // editing host or a table cell/caption element.
        return result.propagateErr();
      }
      // Don't set directionAndAmount to eNone (see Bug 502259)
      break;
    case nsIEditor::ePrevious: {
      // Only extend the selection where the selection is after a UTF-16
      // surrogate pair or a variation selector.
      // For other cases we don't want to do that, in order
      // to make sure that pressing backspace will only delete the last
      // typed character.
      // XXX This is odd if the previous one is a sequence for a grapheme
      //     cluster.
      const auto atStartOfSelection = GetFirstRangeStartPoint<EditorDOMPoint>();
      if (MOZ_UNLIKELY(NS_WARN_IF(!atStartOfSelection.IsSet()))) {
        return Err(NS_ERROR_FAILURE);
      }

      // node might be anonymous DIV, so we find better text node
      const EditorDOMPoint insertionPoint =
          aEditorBase.IsTextEditor()
              ? aEditorBase.AsTextEditor()->FindBetterInsertionPoint(
                    atStartOfSelection)
              : atStartOfSelection.GetPointInTextNodeIfPointingAroundTextNode<
                    EditorDOMPoint>();
      if (MOZ_UNLIKELY(!insertionPoint.IsSet())) {
        NS_WARNING(
            "EditorBase::FindBetterInsertionPoint() failed, but ignored");
        return aDirectionAndAmount;
      }

      if (!insertionPoint.IsInTextNode()) {
        return aDirectionAndAmount;
      }

      const CharacterDataBuffer* data =
          &insertionPoint.ContainerAs<Text>()->DataBuffer();
      uint32_t offset = insertionPoint.Offset();
      if (!(offset > 1 &&
            data->IsLowSurrogateFollowingHighSurrogateAt(offset - 1)) &&
          !(offset > 0 &&
            gfxFontUtils::IsVarSelector(data->CharAt(offset - 1)))) {
        return aDirectionAndAmount;
      }
      // Different from the `eNext` case, we look for character boundary.
      // I'm not sure whether this inconsistency between "Delete" and
      // "Backspace" is intentional or not.
      result = nsFrameSelection::CreateRangeExtendedToPreviousCharacterBoundary<
          dom::Range>(*presShell, limitersAndCaretData, anchorFocusRange,
                      rangeDirection);
      if (NS_WARN_IF(aEditorBase.Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (result.isErr()) [[unlikely]] {
        // Couldn't extend the range to outside the limiter. Don't warn this
        // since the user may press Backspace or Delete at start or end of the
        // editing host or a table cell/caption element.
        return result.propagateErr();
      }
      break;
    }
    case nsIEditor::eToBeginningOfLine:
      result = nsFrameSelection::CreateRangeExtendedToPreviousHardLineBreak<
          dom::Range>(*presShell, limitersAndCaretData, anchorFocusRange,
                      rangeDirection);
      if (NS_WARN_IF(aEditorBase.Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (result.isErr()) [[unlikely]] {
        // Couldn't extend the range to outside the limiter. Don't warn this
        // since the user may press Backspace or Delete at start or end of the
        // editing host or a table cell/caption element.
        return result.propagateErr();
      }
      directionAndAmountResult = nsIEditor::eNone;
      break;
    case nsIEditor::eToEndOfLine:
      result =
          nsFrameSelection::CreateRangeExtendedToNextHardLineBreak<dom::Range>(
              *presShell, limitersAndCaretData, anchorFocusRange,
              rangeDirection);
      if (NS_WARN_IF(aEditorBase.Destroyed())) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      if (result.isErr()) [[unlikely]] {
        // Couldn't extend the range to outside the limiter. Don't warn this
        // since the user may press Backspace or Delete at start or end of the
        // editing host or a table cell/caption element.
        return result.propagateErr();
      }
      directionAndAmountResult = nsIEditor::eNext;
      break;
    default:
      return aDirectionAndAmount;
  }

  if (result.isErr()) {
    return Err(result.inspectErr());
  }
  RefPtr<dom::Range> extendedRange(result.unwrap().forget());
  if (!extendedRange || NS_WARN_IF(!extendedRange->IsPositioned())) {
    NS_WARNING("Failed to extend the range, but ignored");
    return directionAndAmountResult;
  }

  // If the new range isn't editable, keep using the original range.
  if (aEditorBase.IsHTMLEditor() &&
      !AutoClonedRangeArray::IsEditableRange(*extendedRange, *editingHost)) {
    return aDirectionAndAmount;
  }

  if (NS_WARN_IF(!mLimitersAndCaretData.RangeInLimiters(*extendedRange))) {
    NS_WARNING("A range was extended to outer of selection limiter");
    return Err(NS_ERROR_FAILURE);
  }

  // Swap focus/anchor range with the extended range.
  DebugOnly<bool> found = false;
  for (OwningNonNull<dom::Range>& range : mRanges) {
    if (range == mAnchorFocusRange) {
      range = *extendedRange;
      found = true;
      break;
    }
  }
  MOZ_ASSERT(found);
  mAnchorFocusRange.swap(extendedRange);
  return directionAndAmountResult;
}

void AutoClonedSelectionRangeArray::
    MaybeExtendAnchorFocusRangeToDeleteAdjacentWhiteSpace(
        const EditorBase& aEditorBase) {
  if (!StaticPrefs::
          editor_word_select_delete_space_after_doubleclick_selection() ||
      !aEditorBase.IsSelectionCreatedByDoubleclick()) {
    return;
  }
  if (!mAnchorFocusRange || !mAnchorFocusRange->IsPositioned() ||
      mAnchorFocusRange->Collapsed()) {
    return;
  }
  MOZ_ASSERT(mRanges.Length() == 1);
  MOZ_ASSERT(mRanges[0].get() == mAnchorFocusRange.get());

  const auto PreviousCharPoint =
      [&](const EditorRawDOMPoint& aPoint) -> Maybe<EditorRawDOMPoint> {
    if (aEditorBase.IsTextEditor()) {
      if (!aPoint.IsInTextNode() || aPoint.IsStartOfContainer()) {
        return Nothing();
      }
      return Some(aPoint.PreviousPoint());
    }
    const WSScanResult previousThing =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes}, aPoint);
    if (!previousThing.InVisibleOrCollapsibleCharacters() &&
        !previousThing.ReachedPreformattedLineBreak()) {
      return Nothing();
    }
    return Some(previousThing.PointAtReachedContent<EditorRawDOMPoint>());
  };
  const auto InclusiveNextCharPoint =
      [&](const EditorRawDOMPoint& aPoint) -> Maybe<EditorRawDOMPoint> {
    if (aEditorBase.IsTextEditor()) {
      if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
          aPoint.IsCharNewLine()) {
        return Nothing();
      }
      return Some(aPoint);
    }
    const WSScanResult nextThing =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes}, aPoint);
    if (!nextThing.InVisibleOrCollapsibleCharacters()) {
      return Nothing();
    }
    return Some(nextThing.PointAtReachedContent<EditorRawDOMPoint>());
  };
  const auto WhiteSpaceRange =
      [&](const Maybe<EditorRawDOMPoint>& aPoint) -> Maybe<EditorRawDOMRange> {
    if (aPoint.isNothing() || !aPoint->IsInTextNode() ||
        aPoint->IsEndOfContainer() ||
        (!aPoint->IsCharASCIISpaceOrNBSP() &&
         aPoint->Char() != HTMLEditUtils::kFormFeed)) {
      return Nothing();
    }
    if (aEditorBase.IsTextEditor() || !aPoint->IsCharCollapsibleASCIISpace()) {
      return Some(EditorRawDOMRange(*aPoint, aPoint->NextPoint()));
    }
    auto rangeOrError = WSRunScanner::GetRangeInTextNodesToForwardDeleteFrom(
        {WSRunScanner::Option::OnlyEditableNodes},
        aPoint->To<EditorDOMPoint>());
    if (rangeOrError.isErr()) {
      NS_WARNING(
          "WSRunScanner::GetRangeInTextNodesToForwardDeleteFrom() failed");
      return Nothing();
    }
    if (!rangeOrError.inspect().IsPositioned()) {
      return Nothing();
    }
    return Some(EditorRawDOMRange(rangeOrError.inspect()));
  };

  const EditorRawDOMPoint startPoint(mAnchorFocusRange->StartRef());
  const EditorRawDOMPoint endPoint(mAnchorFocusRange->EndRef());
  const Maybe<EditorRawDOMRange> firstWhiteSpace =
      WhiteSpaceRange(InclusiveNextCharPoint(startPoint));
  const Maybe<EditorRawDOMRange> lastWhiteSpace =
      WhiteSpaceRange(PreviousCharPoint(endPoint));
  // Collapsed characters at the start of the range do not select the visible
  // space preceding them.
  if ((firstWhiteSpace && firstWhiteSpace->StartRef().IsBefore(endPoint) &&
       !firstWhiteSpace->StartRef().IsBefore(startPoint)) ||
      (lastWhiteSpace && !lastWhiteSpace->StartRef().IsBefore(startPoint))) {
    return;
  }

  if (const Maybe<EditorRawDOMRange> precedingWhiteSpace =
          WhiteSpaceRange(PreviousCharPoint(startPoint))) {
    const EditorRawDOMPoint& newStart = precedingWhiteSpace->StartRef();
    if (!mLimitersAndCaretData.NodeIsInLimiters(newStart.GetContainer())) {
      return;
    }
    IgnoredErrorResult error;
    mAnchorFocusRange->SetStart(newStart.ToRawRangeBoundary(), error);
    NS_WARNING_ASSERTION(!error.Failed(), "Range::SetStart() failed");
    return;
  }

  if (const Maybe<EditorRawDOMRange> followingWhiteSpace =
          WhiteSpaceRange(InclusiveNextCharPoint(endPoint))) {
    const EditorRawDOMPoint& newEnd = followingWhiteSpace->EndRef();
    if (!mLimitersAndCaretData.NodeIsInLimiters(newEnd.GetContainer())) {
      return;
    }
    IgnoredErrorResult error;
    mAnchorFocusRange->SetEnd(newEnd.ToRawRangeBoundary(), error);
    NS_WARNING_ASSERTION(!error.Failed(), "Range::SetEnd() failed");
  }
}

}  // namespace mozilla
