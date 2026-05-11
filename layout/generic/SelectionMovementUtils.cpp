/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SelectionMovementUtils.h"

#include "ErrorList.h"
#include "WordMovementType.h"
#include "mozilla/CaretAssociationHint.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/Maybe.h"
#include "mozilla/PresShell.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "nsBidiPresUtils.h"
#include "nsBlockFrame.h"
#include "nsCOMPtr.h"
#include "nsCaret.h"
#include "nsFrameSelection.h"
#include "nsFrameTraversal.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsLayoutUtils.h"
#include "nsPresContext.h"
#include "nsTextFrame.h"

namespace mozilla {
using namespace dom;
template Result<RangeBoundary, nsresult>
SelectionMovementUtils::MoveRangeBoundaryToSomewhere(
    const RangeBoundary& aRangeBoundary, nsDirection aDirection,
    CaretAssociationHint aHint, intl::BidiEmbeddingLevel aCaretBidiLevel,
    nsSelectionAmount aAmount, PeekOffsetOptions aOptions,
    const dom::Element* aAncestorLimiter);

template Result<RawRangeBoundary, nsresult>
SelectionMovementUtils::MoveRangeBoundaryToSomewhere(
    const RawRangeBoundary& aRangeBoundary, nsDirection aDirection,
    CaretAssociationHint aHint, intl::BidiEmbeddingLevel aCaretBidiLevel,
    nsSelectionAmount aAmount, PeekOffsetOptions aOptions,
    const dom::Element* aAncestorLimiter);

template <typename ParentType, typename RefType>
Result<RangeBoundaryBase<ParentType, RefType>, nsresult>
SelectionMovementUtils::MoveRangeBoundaryToSomewhere(
    const RangeBoundaryBase<ParentType, RefType>& aRangeBoundary,
    nsDirection aDirection, CaretAssociationHint aHint,
    intl::BidiEmbeddingLevel aCaretBidiLevel, nsSelectionAmount aAmount,
    PeekOffsetOptions aOptions, const dom::Element* aAncestorLimiter) {
  MOZ_ASSERT(aDirection == eDirNext || aDirection == eDirPrevious);
  MOZ_ASSERT(aAmount == eSelectCharacter || aAmount == eSelectCluster ||
             aAmount == eSelectWord || aAmount == eSelectBeginLine ||
             aAmount == eSelectEndLine || aAmount == eSelectParagraph);

  if (!aRangeBoundary.IsSetAndValid()) {
    return Err(NS_ERROR_FAILURE);
  }
  if (!aRangeBoundary.GetContainer()->IsContent()) {
    return Err(NS_ERROR_FAILURE);
  }
  Result<PeekOffsetStruct, nsresult> result = PeekOffsetForCaretMove(
      aRangeBoundary.GetContainer()->AsContent(),
      *aRangeBoundary.Offset(
          RangeBoundaryBase<ParentType,
                            RefType>::OffsetFilter::kValidOrInvalidOffsets),
      aDirection, aHint, aCaretBidiLevel, aAmount, nsPoint{0, 0}, aOptions,
      aAncestorLimiter);
  if (result.isErr()) {
    return Err(NS_ERROR_FAILURE);
  }
  const PeekOffsetStruct& pos = result.unwrap();
  if (NS_WARN_IF(!pos.mResultContent)) {
    return RangeBoundaryBase<ParentType, RefType>{};
  }

  return RangeBoundaryBase<ParentType, RefType>{
      pos.mResultContent, static_cast<uint32_t>(pos.mContentOffset)};
}

// FYI: This was done during a call of GetPrimaryFrameForCaretAtFocusNode.
// Therefore, this may not be intended by the original author.

// static
Result<PeekOffsetStruct, nsresult>
SelectionMovementUtils::PeekOffsetForCaretMove(
    nsIContent* aContent, uint32_t aOffset, nsDirection aDirection,
    CaretAssociationHint aHint, intl::BidiEmbeddingLevel aCaretBidiLevel,
    const nsSelectionAmount aAmount, const nsPoint& aDesiredCaretPos,
    PeekOffsetOptions aOptions, const Element* aAncestorLimiter) {
  const PrimaryFrameData frameForFocus =
      SelectionMovementUtils::GetPrimaryFrameForCaret(
          aContent, aOffset, aOptions.contains(PeekOffsetOption::Visual), aHint,
          aCaretBidiLevel);
  if (!frameForFocus) {
    return Err(NS_ERROR_FAILURE);
  }

  aOptions += {PeekOffsetOption::JumpLines, PeekOffsetOption::IsKeyboardSelect};
  PeekOffsetStruct pos(
      aAmount, aDirection,
      static_cast<int32_t>(frameForFocus.mOffsetInFrameContent),
      aDesiredCaretPos, aOptions, eDefaultBehavior, aAncestorLimiter);
  nsresult rv = frameForFocus->PeekOffset(&pos);
  if (NS_FAILED(rv)) {
    return Err(rv);
  }
  return pos;
}

// static
nsPrevNextBidiLevels SelectionMovementUtils::GetPrevNextBidiLevels(
    nsIContent* aNode, uint32_t aContentOffset, CaretAssociationHint aHint,
    bool aJumpLines, const Element* aAncestorLimiter) {
  // Get the level of the frames on each side
  nsDirection direction;

  nsPrevNextBidiLevels levels{};
  levels.SetData(nullptr, nullptr, intl::BidiEmbeddingLevel::LTR(),
                 intl::BidiEmbeddingLevel::LTR());

  FrameAndOffset currentFrameAndOffset =
      SelectionMovementUtils::GetFrameForNodeOffset(aNode, aContentOffset,
                                                    aHint);
  if (!currentFrameAndOffset) {
    return levels;
  }

  auto [frameStart, frameEnd] = currentFrameAndOffset->GetOffsets();

  if (0 == frameStart && 0 == frameEnd) {
    direction = eDirPrevious;
  } else if (static_cast<uint32_t>(frameStart) ==
             currentFrameAndOffset.mOffsetInFrameContent) {
    direction = eDirPrevious;
  } else if (static_cast<uint32_t>(frameEnd) ==
             currentFrameAndOffset.mOffsetInFrameContent) {
    direction = eDirNext;
  } else {
    // we are neither at the beginning nor at the end of the frame, so we have
    // no worries
    intl::BidiEmbeddingLevel currentLevel =
        currentFrameAndOffset->GetEmbeddingLevel();
    levels.SetData(currentFrameAndOffset.mFrame, currentFrameAndOffset.mFrame,
                   currentLevel, currentLevel);
    return levels;
  }

  PeekOffsetOptions peekOffsetOptions{PeekOffsetOption::StopAtScroller};
  if (aJumpLines) {
    peekOffsetOptions += PeekOffsetOption::JumpLines;
  }
  nsIFrame* newFrame = currentFrameAndOffset
                           ->GetFrameFromDirection(direction, peekOffsetOptions,
                                                   aAncestorLimiter)
                           .mFrame;

  FrameBidiData currentBidi = currentFrameAndOffset->GetBidiData();
  intl::BidiEmbeddingLevel currentLevel = currentBidi.embeddingLevel;
  intl::BidiEmbeddingLevel newLevel =
      newFrame ? newFrame->GetEmbeddingLevel() : currentBidi.baseLevel;

  // If not jumping lines, disregard br frames, since they might be positioned
  // incorrectly.
  // XXX This could be removed once bug 339786 is fixed.
  if (!aJumpLines) {
    if (currentFrameAndOffset->IsBrFrame()) {
      currentFrameAndOffset = {nullptr, 0u};
      currentLevel = currentBidi.baseLevel;
    }
    if (newFrame && newFrame->IsBrFrame()) {
      newFrame = nullptr;
      newLevel = currentBidi.baseLevel;
    }
  }

  if (direction == eDirNext) {
    levels.SetData(currentFrameAndOffset.mFrame, newFrame, currentLevel,
                   newLevel);
  } else {
    levels.SetData(newFrame, currentFrameAndOffset.mFrame, newLevel,
                   currentLevel);
  }

  return levels;
}

// static
Result<nsIFrame*, nsresult> SelectionMovementUtils::GetFrameFromLevel(
    nsIFrame* aFrameIn, nsDirection aDirection,
    intl::BidiEmbeddingLevel aBidiLevel) {
  if (!aFrameIn) {
    return Err(NS_ERROR_NULL_POINTER);
  }

  intl::BidiEmbeddingLevel foundLevel = intl::BidiEmbeddingLevel::LTR();

  nsFrameIterator frameIterator(aFrameIn->PresContext(), aFrameIn,
                                nsFrameIterator::Type::Leaf,
                                false,  // aVisual
                                false,  // aLockInScrollView
                                false,  // aFollowOOFs
                                false   // aSkipPopupChecks
  );

  nsIFrame* foundFrame = aFrameIn;
  nsIFrame* theFrame = nullptr;
  do {
    theFrame = foundFrame;
    foundFrame = frameIterator.Traverse(aDirection == eDirNext);
    if (!foundFrame) {
      return Err(NS_ERROR_FAILURE);
    }
    foundLevel = foundFrame->GetEmbeddingLevel();

  } while (foundLevel > aBidiLevel);

  MOZ_ASSERT(theFrame);
  return theFrame;
}

bool SelectionMovementUtils::AdjustFrameForLineStart(nsIFrame*& aFrame,
                                                     uint32_t& aFrameOffset) {
  if (!aFrame->HasSignificantTerminalNewline()) {
    return false;
  }

  auto [start, end] = aFrame->GetOffsets();
  if (aFrameOffset != static_cast<uint32_t>(end)) {
    return false;
  }

  nsIFrame* nextSibling = aFrame->GetNextSibling();
  if (!nextSibling) {
    return false;
  }

  aFrame = nextSibling;
  std::tie(start, end) = aFrame->GetOffsets();
  aFrameOffset = start;
  return true;
}

static bool IsDisplayContents(const nsIContent* aContent) {
  return aContent->IsElement() && aContent->AsElement()->IsDisplayContents();
}

// static
FrameAndOffset SelectionMovementUtils::GetFrameForNodeOffset(
    const nsIContent* aNode, uint32_t aOffset, CaretAssociationHint aHint) {
  if (!aNode) {
    return {};
  }

  if (static_cast<int32_t>(aOffset) < 0) {
    return {};
  }

  if (!aNode->GetPrimaryFrame() && !IsDisplayContents(aNode)) {
    return {};
  }

  nsIFrame *returnFrame = nullptr, *lastFrame = aNode->GetPrimaryFrame();
  const nsIContent* theNode = nullptr;
  uint32_t offsetInFrameContent, offsetInLastFrameContent = aOffset;

  while (true) {
    if (returnFrame) {
      lastFrame = returnFrame;
      offsetInLastFrameContent = offsetInFrameContent;
    }
    offsetInFrameContent = aOffset;

    theNode = aNode;

    if (aNode->IsElement()) {
      uint32_t childIndex = 0;
      uint32_t numChildren = theNode->GetChildCount();

      if (aHint == CaretAssociationHint::Before) {
        if (aOffset > 0) {
          childIndex = aOffset - 1;
        } else {
          childIndex = aOffset;
        }
      } else {
        MOZ_ASSERT(aHint == CaretAssociationHint::After);
        if (aOffset >= numChildren) {
          if (numChildren > 0) {
            childIndex = numChildren - 1;
          } else {
            childIndex = 0;
          }
        } else {
          childIndex = aOffset;
        }
      }

      if (childIndex > 0 || numChildren > 0) {
        nsCOMPtr<nsIContent> childNode =
            theNode->GetChildAt_Deprecated(childIndex);

        if (!childNode) {
          break;
        }

        theNode = childNode;
      }

      // Now that we have the child node, check if it too
      // can contain children. If so, descend into child.
      if (theNode->IsElement() && theNode->GetChildCount() &&
          !theNode->HasIndependentSelection()) {
        aNode = theNode;
        aOffset = aOffset > childIndex ? theNode->GetChildCount() : 0;
        continue;
      }

      // Check to see if theNode is a text node. If it is, translate
      // aOffset into an offset into the text node.
      if (const Text* textNode = Text::FromNode(theNode)) {
        if (theNode->GetPrimaryFrame()) {
          if (aOffset > childIndex) {
            uint32_t textLength = textNode->Length();

            offsetInFrameContent = textLength;
          } else {
            offsetInFrameContent = 0;
          }
        } else {
          uint32_t numChildren = aNode->GetChildCount();
          uint32_t newChildIndex = aHint == CaretAssociationHint::Before
                                       ? childIndex - 1
                                       : childIndex + 1;

          if (newChildIndex < numChildren) {
            nsCOMPtr<nsIContent> newChildNode =
                aNode->GetChildAt_Deprecated(newChildIndex);
            if (!newChildNode) {
              return {};
            }

            aNode = newChildNode;
            aOffset = aHint == CaretAssociationHint::Before
                          ? aNode->GetChildCount()
                          : 0;
            continue;
          }  // newChildIndex is illegal which means we're at first or last
          // child. Just use original node to get the frame.
          theNode = aNode;
        }
      }
    }

    // If the node is a ShadowRoot, the frame needs to be adjusted,
    // because a ShadowRoot does not get a frame. Its children are rendered
    // as children of the host.
    if (const ShadowRoot* shadow = ShadowRoot::FromNode(theNode)) {
      theNode = shadow->GetHost();
    }

    returnFrame = theNode->GetPrimaryFrame();
    if (returnFrame) {
      // FIXME: offsetInFrameContent has not been updated for theNode yet when
      // theNode is different from aNode. E.g., if a child at aNode and aOffset
      // is an <img>, theNode is now the <img> but offsetInFrameContent is the
      // offset for aNode.
      break;
    }

    if (aHint == CaretAssociationHint::Before) {
      if (aOffset > 0) {
        --aOffset;
        continue;
      }
      break;
    }
    if (aOffset < theNode->GetChildCount()) {
      ++aOffset;
      continue;
    }
    break;
  }  // end while

  if (!returnFrame) {
    if (!lastFrame) {
      return {};
    }
    returnFrame = lastFrame;
    offsetInFrameContent = offsetInLastFrameContent;
  }

  // If we ended up here and were asked to position the caret after a visible
  // break, let's return the frame on the next line instead if it exists.
  if (aOffset > 0 && (uint32_t)aOffset >= aNode->Length() &&
      theNode == aNode->GetLastChild()) {
    nsIFrame* newFrame;
    nsLayoutUtils::IsInvisibleBreak(theNode, &newFrame);
    if (newFrame) {
      returnFrame = newFrame;
      offsetInFrameContent = 0;
    }
  }

  // find the child frame containing the offset we want
  int32_t unused = 0;
  returnFrame->GetChildFrameContainingOffset(
      static_cast<int32_t>(offsetInFrameContent),
      aHint == CaretAssociationHint::After, &unused, &returnFrame);
  return {returnFrame, offsetInFrameContent};
}

// static
RawRangeBoundary SelectionMovementUtils::GetFirstVisiblePointAtLeaf(
    const AbstractRange& aRange) {
  MOZ_ASSERT(aRange.IsPositioned());
  MOZ_ASSERT_IF(aRange.IsStaticRange(), aRange.AsStaticRange()->IsValid());

  // Currently, this is designed for non-collapsed range because this tries to
  // return a point in aRange.  Therefore, if we need to return a nearest point
  // even outside aRange, we should add another utility method for making it
  // accept the outer range.
  MOZ_ASSERT(!aRange.Collapsed());

  // The result should be a good point to put a UI to show something about the
  // start boundary of aRange.  Therefore, we should find a content which is
  // visible or first unselectable one.

  // FIXME: ContentIterator does not support iterating content across shadow DOM
  // boundaries.  We should improve it and here support it as an option.

  // If the start boundary is in a visible and selectable `Text`, let's return
  // the start boundary as-is.
  if (Text* const text = Text::FromNode(aRange.GetStartContainer())) {
    nsIFrame* const textFrame = text->GetPrimaryFrame();
    if (textFrame && textFrame->IsSelectable()) {
      return aRange.StartRef().AsRaw();
    }
  }

  // Iterate start of each node in the range so that the following loop checks
  // containers first, then, inner containers and leaf nodes.
  UnsafePreContentIterator iter;
  if (aRange.IsDynamicRange()) {
    if (NS_WARN_IF(NS_FAILED(iter.InitWithoutValidatingPoints(
            aRange.StartRef().AsRaw(), aRange.EndRef().AsRaw())))) {
      return {nullptr, nullptr};
    }
  } else {
    if (NS_WARN_IF(NS_FAILED(
            iter.Init(aRange.StartRef().AsRaw(), aRange.EndRef().AsRaw())))) {
      return {nullptr, nullptr};
    }
  }

  // We need to ignore unselectable nodes if the range started from an
  // unselectable node, for example, if starting from the document start but
  // only in <dialog> which is shown as a modal one is selectable, we want to
  // treat the visible selection starts from the start of the first visible
  // thing in the <dialog>.
  // Additionally, let's stop when we find first unselectable element in a
  // selectable node.  Then, the caller can show something at the end edge of
  // the unselectable element rather than the leaf to make it clear that the
  // selection range starts before the unselectable element.
  bool foundSelectableContainer = [&]() {
    nsIContent* const startContainer =
        nsIContent::FromNode(aRange.GetStartContainer());
    return startContainer && startContainer->IsSelectable();
  }();
  for (iter.First(); !iter.IsDone(); iter.Next()) {
    nsIContent* const content =
        nsIContent::FromNodeOrNull(iter.GetCurrentNode());
    if (MOZ_UNLIKELY(!content)) {
      break;
    }
    nsIFrame* const primaryFrame = content->GetPrimaryFrame();
    // If the content does not have any layout information, let's continue.
    if (!primaryFrame) {
      continue;
    }

    // FYI: We don't need to skip invisible <br> at scanning start of visible
    // thing like what we're doing in GetVisibleRangeEnd() because if we reached
    // it, the selection range starts from end of the line so that putting UI
    // around it is reasonable.

    // If the frame is unselectable, we need to stop scanning now if we're
    // scanning in a selectable range.
    if (!primaryFrame->IsSelectable()) {
      // If we have not found a selectable content yet (this is the case when
      // only a part of the document is selectable like the <dialog> case
      // explained above), we should just ignore the unselectable content until
      // we find first selectable element.  Then, the caller can show something
      // before the first child of the first selectable container in the range.
      if (!foundSelectableContainer) {
        continue;
      }
      // If we have already found a selectable content and now we reached an
      // unselectable element, we should return the point of the unselectable
      // element.  Then, the caller can show something at the start edge of the
      // unselectable element to show users that the range contains the
      // unselectable element.
      return {content->GetParentNode(), content->GetPreviousSibling()};
    }
    // We found a visible (and maybe selectable) Text, return the start of it.
    if (content->IsText()) {
      return {content, 0u};
    }
    // We found a replaced element such as <br>, <img>, form widget return the
    // point at the content.
    if (primaryFrame->IsReplaced()) {
      return {content->GetParentNode(), content->GetPreviousSibling()};
    }
    // <button> is a special case, whose frame is not treated as a replaced
    // element, but we don't want to shrink the range into it.
    if (content->IsHTMLElement(nsGkAtoms::button)) {
      return {content->GetParentNode(), content->GetPreviousSibling()};
    }
    // We found a leaf node like <span></span>.  Return start of it.
    if (!content->HasChildren()) {
      return {content, 0u};
    }
    foundSelectableContainer = true;
  }
  // If there is no visible and selectable things but the start container is
  // selectable, return the original point as is.
  if (foundSelectableContainer) {
    return aRange.StartRef().AsRaw();
  }
  // If the range is completely invisible, return unset boundary.
  return {nullptr, nullptr};
}

// static
RawRangeBoundary SelectionMovementUtils::GetLastVisiblePointAtLeaf(
    const AbstractRange& aRange) {
  MOZ_ASSERT(aRange.IsPositioned());
  MOZ_ASSERT_IF(aRange.IsStaticRange(), aRange.AsStaticRange()->IsValid());

  // Currently, this is designed for non-collapsed range because this tries to
  // return a point in aRange.  Therefore, if we need to return a nearest point
  // even outside aRange, we should add another utility method for making it
  // accept the outer range.
  MOZ_ASSERT(!aRange.Collapsed());

  // The result should be a good point to put a UI to show something about the
  // end boundary of aRange.  Therefore, we should find a leaf content which is
  // visible or first unselectable one.

  // FIXME: ContentIterator does not support iterating content across shadow DOM
  // boundaries.  We should improve it and here support it as an option.

  // If the end boundary is in a visible and selectable `Text`, let's return the
  // end boundary as-is.
  if (Text* const text = Text::FromNode(aRange.GetEndContainer())) {
    nsIFrame* const textFrame = text->GetPrimaryFrame();
    if (textFrame && textFrame->IsSelectable()) {
      return aRange.EndRef().AsRaw();
    }
  }

  // Iterate end of each node in the range so that the following loop checks
  // containers first, then, inner containers and leaf nodes.
  UnsafePostContentIterator iter;
  if (aRange.IsDynamicRange()) {
    if (NS_WARN_IF(NS_FAILED(iter.InitWithoutValidatingPoints(
            aRange.StartRef().AsRaw(), aRange.EndRef().AsRaw())))) {
      return {nullptr, nullptr};
    }
  } else {
    if (NS_WARN_IF(NS_FAILED(
            iter.Init(aRange.StartRef().AsRaw(), aRange.EndRef().AsRaw())))) {
      return {nullptr, nullptr};
    }
  }

  // We need to ignore unselectable nodes if the range ends in an unselectable
  // node, for example, if ending at the document end but only in <dialog> which
  // is shown as a modal one, is selectable, we want to treat the visible
  // selection ends at the end of the last visible thing in the <dialog>.
  // Additionally, let's stop when we find first unselectable element in a
  // selectable node.  Then, the caller can show something at the end edge of
  // the unselectable element rather than the leaf to make it clear that the
  // selection range ends are the unselectable element.
  bool foundSelectableContainer = [&]() {
    nsIContent* const endContainer =
        nsIContent::FromNode(aRange.GetEndContainer());
    return endContainer && endContainer->IsSelectable();
  }();
  for (iter.Last(); !iter.IsDone(); iter.Prev()) {
    nsIContent* const content =
        nsIContent::FromNodeOrNull(iter.GetCurrentNode());
    if (!content) {
      break;
    }
    nsIFrame* const primaryFrame = content->GetPrimaryFrame();
    // If the content does not have any layout information, let's continue.
    if (!primaryFrame) {
      continue;
    }
    // If we reached an invisible <br>, we should skip it because
    // AccessibleCaretManager wants to put the caret for end boundary before the
    // <br> instead of at the end edge of the block.
    if (nsLayoutUtils::IsInvisibleBreak(content)) {
      if (primaryFrame->IsSelectable()) {
        foundSelectableContainer = true;
      }
      continue;
    }
    // If the frame is unselectable, we need to stop scanning now if we're
    // scanning in a selectable range.
    if (!primaryFrame->IsSelectable()) {
      // If we have not found a selectable content yet (this is the case when
      // only a part of the document is selectable like the <dialog> case
      // explained above), we should just ignore the unselectable content until
      // we find first selectable element.  Then, the caller can show something
      // after the last child of the last selectable container in the range.
      if (!foundSelectableContainer) {
        continue;
      }
      // If we have already found a selectable content and now we reached an
      // unselectable element, we should return the point after the unselectable
      // element.  Then, the caller can show something at the end edge of the
      // unselectable element to show users that the range contains the
      // unselectable element.
      return {content->GetParentNode(), content};
    }
    // We found a visible (and maybe selectable) Text, return the end of it.
    if (Text* const text = Text::FromNode(content)) {
      return {text, text->TextDataLength()};
    }
    // We found a replaced element such as <br>, <img>, form widget return the
    // point after the content.
    if (primaryFrame->IsReplaced()) {
      return {content->GetParentNode(), content};
    }
    // <button> is a special case, whose frame is not treated as a replaced
    // element, but we don't want to shrink the range into it.
    if (content->IsHTMLElement(nsGkAtoms::button)) {
      return {content->GetParentNode(), content};
    }
    // We found a leaf node like <span></span>.  Return end of it.
    if (!content->HasChildren()) {
      return {content, 0u};
    }
    foundSelectableContainer = true;
  }
  // If there is no visible and selectable things but the end container is
  // selectable, return the original point as is.
  if (foundSelectableContainer) {
    return aRange.EndRef().AsRaw();
  }
  // If the range is completely invisible, return unset boundary.
  return {nullptr, nullptr};
}

/**
 * Find the first frame in an in-order traversal of the frame subtree rooted
 * at aFrame which is either a text frame logically at the end of a line,
 * or which is aStopAtFrame. Return null if no such frame is found. We don't
 * descend into the children of non-eLineParticipant frames.
 */
static nsIFrame* CheckForTrailingTextFrameRecursive(nsIFrame* aFrame,
                                                    nsIFrame* aStopAtFrame) {
  if (aFrame == aStopAtFrame ||
      ((aFrame->IsTextFrame() &&
        (static_cast<nsTextFrame*>(aFrame))->IsAtEndOfLine()))) {
    return aFrame;
  }
  if (!aFrame->IsLineParticipant()) {
    return nullptr;
  }

  for (nsIFrame* f : aFrame->PrincipalChildList()) {
    if (nsIFrame* r = CheckForTrailingTextFrameRecursive(f, aStopAtFrame)) {
      return r;
    }
  }
  return nullptr;
}

static nsLineBox* FindContainingLine(nsIFrame* aFrame) {
  while (aFrame && aFrame->IsLineParticipant()) {
    nsIFrame* parent = aFrame->GetParent();
    nsBlockFrame* blockParent = do_QueryFrame(parent);
    if (blockParent) {
      bool isValid;
      nsBlockInFlowLineIterator iter(blockParent, aFrame, &isValid);
      return isValid ? iter.GetLine().get() : nullptr;
    }
    aFrame = parent;
  }
  return nullptr;
}

static void AdjustCaretFrameForLineEnd(nsIFrame** aFrame, uint32_t* aOffset,
                                       bool aEditableOnly) {
  nsLineBox* line = FindContainingLine(*aFrame);
  if (!line) {
    return;
  }
  for (nsIFrame* f : line->ChildFrames()) {
    nsIFrame* r = CheckForTrailingTextFrameRecursive(f, *aFrame);
    if (r == *aFrame) {
      return;
    }
    if (!r) {
      continue;
    }
    // If found text frame is non-editable but the start frame content is
    // editable, we don't want to put caret into the non-editable text node.
    // We should return the given frame as-is in this case.
    if (aEditableOnly && !r->GetContent()->IsEditable()) {
      return;
    }
    // We found our frame.
    MOZ_ASSERT(r->IsTextFrame(), "Expected text frame");
    *aFrame = r;
    *aOffset = (static_cast<nsTextFrame*>(r))->GetContentEnd();
    return;
    // FYI: Setting the caret association hint was done during a call of
    // GetPrimaryFrameForCaretAtFocusNode.  Therefore, this may not be intended
    // by the original author.
  }
}

CaretFrameData SelectionMovementUtils::GetCaretFrameForNodeOffset(
    const nsFrameSelection* aFrameSelection, nsIContent* aContentNode,
    uint32_t aOffset, CaretAssociationHint aFrameHint,
    intl::BidiEmbeddingLevel aBidiLevel,
    ForceEditableRegion aForceEditableRegion) {
  if (!aContentNode || !aContentNode->IsInComposedDoc()) {
    return {};
  }

  CaretFrameData result;
  result.mHint = aFrameHint;
  if (aFrameSelection) {
    PresShell* presShell = aFrameSelection->GetPresShell();
    if (!presShell) {
      return {};
    }

    if (!aContentNode || !aContentNode->IsInComposedDoc() ||
        presShell->GetDocument() != aContentNode->GetComposedDoc()) {
      return {};
    }

    result.mHint = aFrameSelection->GetHint();
  }

  MOZ_ASSERT_IF(aForceEditableRegion == ForceEditableRegion::Yes,
                aContentNode->IsEditable());

  const FrameAndOffset frameAndOffset =
      SelectionMovementUtils::GetFrameForNodeOffset(aContentNode, aOffset,
                                                    aFrameHint);
  if (!frameAndOffset) {
    return {};
  }
  result.mFrame = result.mUnadjustedFrame = frameAndOffset.mFrame;
  result.mOffsetInFrameContent = frameAndOffset.mOffsetInFrameContent;

  if (SelectionMovementUtils::AdjustFrameForLineStart(
          result.mFrame, result.mOffsetInFrameContent)) {
    result.mHint = CaretAssociationHint::After;
  } else {
    // if the frame is after a text frame that's logically at the end of the
    // line (e.g. if the frame is a <br> frame), then put the caret at the end
    // of that text frame instead. This way, the caret will be positioned as if
    // trailing whitespace was not trimmed.
    AdjustCaretFrameForLineEnd(
        &result.mFrame, &result.mOffsetInFrameContent,
        aForceEditableRegion == ForceEditableRegion::Yes);
  }

  // Mamdouh : modification of the caret to work at rtl and ltr with Bidi
  //
  // Direction Style from visibility->mDirection
  // ------------------
  if (!result->PresContext()->BidiEnabled()) {
    return result;
  }

  // If there has been a reflow, take the caret Bidi level to be the level of
  // the current frame
  if (aBidiLevel & BIDI_LEVEL_UNDEFINED) {
    aBidiLevel = result->GetEmbeddingLevel();
  }

  nsIFrame* frameBefore;
  nsIFrame* frameAfter;
  intl::BidiEmbeddingLevel
      levelBefore;  // Bidi level of the character before the caret
  intl::BidiEmbeddingLevel
      levelAfter;  // Bidi level of the character after the caret

  auto [start, end] = result->GetOffsets();
  if (start == 0 || end == 0 ||
      static_cast<uint32_t>(start) == result.mOffsetInFrameContent ||
      static_cast<uint32_t>(end) == result.mOffsetInFrameContent) {
    nsPrevNextBidiLevels levels = SelectionMovementUtils::GetPrevNextBidiLevels(
        aContentNode, aOffset, result.mHint, false,
        aFrameSelection
            ? aFrameSelection
                  ->GetAncestorLimiterOrIndependentSelectionRootElement()
            : nullptr);

    /* Boundary condition, we need to know the Bidi levels of the characters
     * before and after the caret */
    if (levels.mFrameBefore || levels.mFrameAfter) {
      frameBefore = levels.mFrameBefore;
      frameAfter = levels.mFrameAfter;
      levelBefore = levels.mLevelBefore;
      levelAfter = levels.mLevelAfter;

      if ((levelBefore != levelAfter) || (aBidiLevel != levelBefore)) {
        aBidiLevel =
            std::max(aBidiLevel, std::min(levelBefore, levelAfter));  // rule c3
        aBidiLevel =
            std::min(aBidiLevel, std::max(levelBefore, levelAfter));  // rule c4
        if (aBidiLevel == levelBefore ||                              // rule c1
            (aBidiLevel > levelBefore && aBidiLevel < levelAfter &&
             aBidiLevel.IsSameDirection(levelBefore)) ||  // rule c5
            (aBidiLevel < levelBefore && aBidiLevel > levelAfter &&
             aBidiLevel.IsSameDirection(levelBefore)))  // rule c9
        {
          if (result.mFrame != frameBefore) {
            if (frameBefore) {  // if there is a frameBefore, move into it
              result.mFrame = frameBefore;
              std::tie(start, end) = result->GetOffsets();
              result.mOffsetInFrameContent = end;
            } else {
              // if there is no frameBefore, we must be at the beginning of
              // the line so we stay with the current frame. Exception: when
              // the first frame on the line has a different Bidi level from
              // the paragraph level, there is no real frame for the caret to
              // be in. We have to find the visually first frame on the line.
              intl::BidiEmbeddingLevel baseLevel = frameAfter->GetBaseLevel();
              if (baseLevel != levelAfter) {
                PeekOffsetStruct pos(eSelectBeginLine, eDirPrevious, 0,
                                     nsPoint(0, 0),
                                     {PeekOffsetOption::StopAtScroller,
                                      PeekOffsetOption::Visual});
                if (NS_SUCCEEDED(frameAfter->PeekOffset(&pos))) {
                  result.mFrame = pos.mResultFrame;
                  result.mOffsetInFrameContent = pos.mContentOffset;
                }
              }
            }
          }
        } else if (aBidiLevel == levelAfter ||  // rule c2
                   (aBidiLevel > levelBefore && aBidiLevel < levelAfter &&
                    aBidiLevel.IsSameDirection(levelAfter)) ||  // rule c6
                   (aBidiLevel < levelBefore && aBidiLevel > levelAfter &&
                    aBidiLevel.IsSameDirection(levelAfter)))  // rule c10
        {
          if (result.mFrame != frameAfter) {
            if (frameAfter) {
              // if there is a frameAfter, move into it
              result.mFrame = frameAfter;
              std::tie(start, end) = result->GetOffsets();
              result.mOffsetInFrameContent = start;
            } else {
              // if there is no frameAfter, we must be at the end of the line
              // so we stay with the current frame.
              // Exception: when the last frame on the line has a different
              // Bidi level from the paragraph level, there is no real frame
              // for the caret to be in. We have to find the visually last
              // frame on the line.
              intl::BidiEmbeddingLevel baseLevel = frameBefore->GetBaseLevel();
              if (baseLevel != levelBefore) {
                PeekOffsetStruct pos(eSelectEndLine, eDirNext, 0, nsPoint(0, 0),
                                     {PeekOffsetOption::StopAtScroller,
                                      PeekOffsetOption::Visual});
                if (NS_SUCCEEDED(frameBefore->PeekOffset(&pos))) {
                  result.mFrame = pos.mResultFrame;
                  result.mOffsetInFrameContent = pos.mContentOffset;
                }
              }
            }
          }
        } else if (aBidiLevel > levelBefore &&
                   aBidiLevel < levelAfter &&  // rule c7/8
                   // before and after have the same parity
                   levelBefore.IsSameDirection(levelAfter) &&
                   // caret has different parity
                   !aBidiLevel.IsSameDirection(levelAfter)) {
          MOZ_ASSERT_IF(aFrameSelection && aFrameSelection->GetPresShell(),
                        aFrameSelection->GetPresShell()->GetPresContext() ==
                            frameAfter->PresContext());
          Result<nsIFrame*, nsresult> frameOrError =
              SelectionMovementUtils::GetFrameFromLevel(frameAfter, eDirNext,
                                                        aBidiLevel);
          if (MOZ_LIKELY(frameOrError.isOk())) {
            result.mFrame = frameOrError.unwrap();
            std::tie(start, end) = result->GetOffsets();
            levelAfter = result->GetEmbeddingLevel();
            if (aBidiLevel.IsRTL()) {
              // c8: caret to the right of the rightmost character
              result.mOffsetInFrameContent = levelAfter.IsRTL() ? start : end;
            } else {
              // c7: caret to the left of the leftmost character
              result.mOffsetInFrameContent = levelAfter.IsRTL() ? end : start;
            }
          }
        } else if (aBidiLevel < levelBefore &&
                   aBidiLevel > levelAfter &&  // rule c11/12
                   // before and after have the same parity
                   levelBefore.IsSameDirection(levelAfter) &&
                   // caret has different parity
                   !aBidiLevel.IsSameDirection(levelAfter)) {
          MOZ_ASSERT_IF(aFrameSelection && aFrameSelection->GetPresShell(),
                        aFrameSelection->GetPresShell()->GetPresContext() ==
                            frameBefore->PresContext());
          Result<nsIFrame*, nsresult> frameOrError =
              SelectionMovementUtils::GetFrameFromLevel(
                  frameBefore, eDirPrevious, aBidiLevel);
          if (MOZ_LIKELY(frameOrError.isOk())) {
            result.mFrame = frameOrError.unwrap();
            std::tie(start, end) = result->GetOffsets();
            levelBefore = result->GetEmbeddingLevel();
            if (aBidiLevel.IsRTL()) {
              // c12: caret to the left of the leftmost character
              result.mOffsetInFrameContent = levelBefore.IsRTL() ? end : start;
            } else {
              // c11: caret to the right of the rightmost character
              result.mOffsetInFrameContent = levelBefore.IsRTL() ? start : end;
            }
          }
        }
      }
    }
  }

  return result;
}

// static
PrimaryFrameData SelectionMovementUtils::GetPrimaryFrameForCaret(
    nsIContent* aContent, uint32_t aOffset, bool aVisual,
    CaretAssociationHint aHint, intl::BidiEmbeddingLevel aCaretBidiLevel) {
  MOZ_ASSERT(aContent);

  {
    const PrimaryFrameData result =
        SelectionMovementUtils::GetPrimaryOrCaretFrameForNodeOffset(
            aContent, aOffset, aVisual, aHint, aCaretBidiLevel);
    if (result) {
      return result;
    }
  }

  // If aContent is whitespace only, we promote focus node to parent because
  // whitespace only node might have no frame.

  if (!aContent->TextIsOnlyWhitespace()) {
    return {};
  }

  nsIContent* parent = aContent->GetParent();
  if (NS_WARN_IF(!parent)) {
    return {};
  }
  const Maybe<uint32_t> offset = parent->ComputeIndexOf(aContent);
  if (NS_WARN_IF(offset.isNothing())) {
    return {};
  }
  return SelectionMovementUtils::GetPrimaryOrCaretFrameForNodeOffset(
      parent, *offset, aVisual, aHint, aCaretBidiLevel);
}

// static
PrimaryFrameData SelectionMovementUtils::GetPrimaryOrCaretFrameForNodeOffset(
    nsIContent* aContent, uint32_t aOffset, bool aVisual,
    CaretAssociationHint aHint, intl::BidiEmbeddingLevel aCaretBidiLevel) {
  if (aVisual) {
    const CaretFrameData result =
        SelectionMovementUtils::GetCaretFrameForNodeOffset(
            nullptr, aContent, aOffset, aHint, aCaretBidiLevel,
            aContent && aContent->IsEditable() ? ForceEditableRegion::Yes
                                               : ForceEditableRegion::No);
    return result;
  }

  return {
      SelectionMovementUtils::GetFrameForNodeOffset(aContent, aOffset, aHint),
      aHint};
}

}  // namespace mozilla
