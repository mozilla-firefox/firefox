/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "TextDirectiveUtil.h"

#include "ContentIterator.h"
#include "Document.h"
#include "Text.h"
#include "fragmentdirectives_ffi_generated.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/SelectionMovementUtils.h"
#include "mozilla/intl/WordBreaker.h"
#include "nsComputedDOMStyle.h"
#include "nsDOMAttributeMap.h"
#include "nsFind.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"
#include "nsINode.h"
#include "nsIURI.h"
#include "nsRange.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsUnicharUtils.h"

namespace mozilla::dom {
LazyLogModule gFragmentDirectiveLog("FragmentDirective");

/* static */
Result<nsString, ErrorResult> TextDirectiveUtil::RangeContentAsString(
    AbstractRange* aRange) {
  nsString content;
  if (!aRange || aRange->Collapsed()) {
    return content;
  }
  UnsafePreContentIterator iter;
  nsresult rv = iter.Init(aRange);
  if (NS_FAILED(rv)) {
    return Err(ErrorResult(rv));
  }
  for (; !iter.IsDone(); iter.Next()) {
    nsINode* current = iter.GetCurrentNode();
    if (!TextDirectiveUtil::NodeIsVisibleTextNode(*current) ||
        TextDirectiveUtil::NodeIsPartOfNonSearchableSubTree(*current)) {
      continue;
    }
    const uint32_t startOffset =
        current == aRange->GetStartContainer() ? aRange->StartOffset() : 0;
    const uint32_t endOffset =
        std::min(current == aRange->GetEndContainer() ? aRange->EndOffset()
                                                      : current->Length(),
                 current->Length());
    const Text* text = Text::FromNode(current);
    text->DataBuffer().AppendTo(content, startOffset, endOffset - startOffset);
  }
  content.CompressWhitespace();
  return content;
}

/* static */ bool TextDirectiveUtil::NodeIsVisibleTextNode(
    const nsINode& aNode) {
  const Text* text = Text::FromNode(aNode);
  if (!text) {
    return false;
  }
  const nsIFrame* frame = text->GetPrimaryFrame();
  return frame && frame->StyleVisibility()->IsVisible();
}

/* static */ RefPtr<nsRange> TextDirectiveUtil::FindStringInRange(
    nsFind* aFinder, const RangeBoundary& aSearchStart,
    const RangeBoundary& aSearchEnd, const nsAString& aQuery,
    bool aWordStartBounded, bool aWordEndBounded) {
  MOZ_DIAGNOSTIC_ASSERT(aFinder);
  TEXT_FRAGMENT_LOG("query='{}', wordStartBounded='{}', wordEndBounded='{}'.\n",
                    NS_ConvertUTF16toUTF8(aQuery), aWordStartBounded,
                    aWordEndBounded);
  aFinder->SetWordStartBounded(aWordStartBounded);
  aFinder->SetWordEndBounded(aWordEndBounded);
  aFinder->SetCaseSensitive(false);
  RefPtr<nsRange> result =
      aFinder->FindFromRangeBoundaries(aQuery, aSearchStart, aSearchEnd);
  if (!result || result->Collapsed()) {
    TEXT_FRAGMENT_LOG("Did not find query '{}'", NS_ConvertUTF16toUTF8(aQuery));
  } else {
    auto rangeToString = [](nsRange* range) -> nsCString {
      nsString rangeString;
      range->ToString(rangeString, IgnoreErrors());
      return NS_ConvertUTF16toUTF8(rangeString);
    };
    TEXT_FRAGMENT_LOG("find returned '{}'", rangeToString(result));
  }
  return result;
}

/* static */ bool TextDirectiveUtil::IsWhitespaceAtPosition(const Text* aText,
                                                            uint32_t aPos) {
  if (!aText || aText->Length() == 0 || aPos >= aText->Length()) {
    return false;
  }
  const CharacterDataBuffer& characterDataBuffer = aText->DataBuffer();
  const char NBSP_CHAR = char(0xA0);
  if (characterDataBuffer.Is2b()) {
    const char16_t* content = characterDataBuffer.Get2b();
    return IsSpaceCharacter(content[aPos]) ||
           content[aPos] == char16_t(NBSP_CHAR);
  }
  const char* content = characterDataBuffer.Get1b();
  return IsSpaceCharacter(content[aPos]) || content[aPos] == NBSP_CHAR;
}

/* static */ bool TextDirectiveUtil::NodeIsSearchInvisible(nsINode& aNode) {
  if (!aNode.IsElement()) {
    return false;
  }
  // 2. If the node serializes as void.
  nsAtom* nodeNameAtom = aNode.NodeInfo()->NameAtom();
  if (FragmentOrElement::IsHTMLVoid(nodeNameAtom)) {
    return true;
  }
  // 3. Is any of the following types: HTMLIFrameElement, HTMLImageElement,
  // HTMLMeterElement, HTMLObjectElement, HTMLProgressElement, HTMLStyleElement,
  // HTMLScriptElement, HTMLVideoElement, HTMLAudioElement
  if (aNode.IsAnyOfHTMLElements(
          nsGkAtoms::iframe, nsGkAtoms::image, nsGkAtoms::meter,
          nsGkAtoms::object, nsGkAtoms::progress, nsGkAtoms::style,
          nsGkAtoms::script, nsGkAtoms::video, nsGkAtoms::audio)) {
    return true;
  }
  // 4. Is a select element whose multiple content attribute is absent.
  if (aNode.IsHTMLElement(nsGkAtoms::select)) {
    return aNode.GetAttributes()->GetNamedItem(u"multiple"_ns) == nullptr;
  }
  // This is tested last because it's the most expensive check.
  // 1. The computed value of its 'display' property is 'none'.
  const Element* nodeAsElement = Element::FromNode(aNode);
  const RefPtr<const ComputedStyle> computedStyle =
      nsComputedDOMStyle::GetComputedStyleNoFlush(nodeAsElement);
  return !computedStyle ||
         computedStyle->StyleDisplay()->mDisplay == StyleDisplay::None;
}

/* static */ bool TextDirectiveUtil::NodeHasBlockLevelDisplay(nsINode& aNode) {
  if (!aNode.IsElement()) {
    return false;
  }
  // <br> is inline but creates a line break that should act as a block boundary
  // for text fragment purposes. Without this, text on both sides of a <br> gets
  // concatenated (e.g. "test<br>test" -> "testtest").
  if (aNode.IsHTMLElement(nsGkAtoms::br)) {
    return true;
  }
  const Element* nodeAsElement = Element::FromNode(aNode);
  const RefPtr<const ComputedStyle> computedStyle =
      nsComputedDOMStyle::GetComputedStyleNoFlush(nodeAsElement);
  if (!computedStyle) {
    return false;
  }
  const StyleDisplay& styleDisplay = computedStyle->StyleDisplay()->mDisplay;
  return styleDisplay == StyleDisplay::Block ||
         styleDisplay == StyleDisplay::Table ||
         styleDisplay == StyleDisplay::TableCell ||
         styleDisplay == StyleDisplay::FlowRoot ||
         styleDisplay == StyleDisplay::Grid ||
         styleDisplay == StyleDisplay::Flex || styleDisplay.IsListItem();
}

/* static */ nsINode* TextDirectiveUtil::GetBlockAncestorForNode(
    nsINode* aNode) {
  // 1. Let curNode be node.
  RefPtr<nsINode> curNode = aNode;
  // 2. While curNode is non-null
  while (curNode) {
    // 2.1. If curNode is not a Text node and it has block-level display then
    // return curNode.
    if (!curNode->IsText() && NodeHasBlockLevelDisplay(*curNode)) {
      return curNode;
    }
    // 2.2. Otherwise, set curNode to curNode’s parent.
    curNode = curNode->GetParentNode();
  }
  // 3.Return node’s node document's document element.
  return aNode->GetOwnerDocument();
}

/* static */ bool TextDirectiveUtil::NodeIsPartOfNonSearchableSubTree(
    nsINode& aNode) {
  nsINode* node = &aNode;
  do {
    if (NodeIsSearchInvisible(*node)) {
      return true;
    }
  } while ((node = node->GetParentOrShadowHostNode()));
  return false;
}

/* static */ bool TextDirectiveUtil::AdvanceStartToNextNonWhitespacePosition(
    nsRange& aRange) {
  // 1. While range is not collapsed:
  while (!aRange.Collapsed()) {
    // 1.1. Let node be range's start node.
    RefPtr<nsINode> node = aRange.GetStartContainer();
    MOZ_ASSERT(node);
    // 1.2. Let offset be range's start offset.
    const uint32_t offset = aRange.StartOffset();
    // 1.3. If node is part of a non-searchable subtree or if node is not a
    // visible text node or if offset is equal to node's length then:
    if (NodeIsPartOfNonSearchableSubTree(*node) ||
        !NodeIsVisibleTextNode(*node) || offset == node->Length()) {
      // 1.3.1. Set range's start node to the next node, in shadow-including
      // tree order.
      // 1.3.2. Set range's start offset to 0.
      if (NS_FAILED(aRange.SetStart(node->GetNextNode(), 0))) {
        return false;
      }
      // 1.3.3. Continue.
      continue;
    }
    const Text* text = Text::FromNode(node);
    MOZ_ASSERT(text);
    // These steps are moved to `IsWhitespaceAtPosition()`.
    // 1.4. If the substring data of node at offset offset and count 6 is equal
    // to the string "&nbsp;" then:
    // 1.4.1. Add 6 to range’s start offset.
    // 1.5. Otherwise, if the substring data of node at offset offset and count
    // 5 is equal to the string "&nbsp" then:
    // 1.5.1. Add 5 to range’s start offset.
    // 1.6. Otherwise:
    // 1.6.1 Let cp be the code point at the offset index in node’s data.
    // 1.6.2 If cp does not have the White_Space property set, return.
    // 1.6.3 Add 1 to range’s start offset.
    if (!IsWhitespaceAtPosition(text, offset)) {
      return true;
    }

    aRange.SetStart(node, offset + 1);
  }
  return false;
}
// https://wicg.github.io/scroll-to-text-fragment/#find-a-range-from-a-text-directive
// Steps 2.2.3, 2.3.4
/* static */
RangeBoundary TextDirectiveUtil::MoveToNextBoundaryPoint(
    const RangeBoundary& aPoint) {
  MOZ_DIAGNOSTIC_ASSERT(aPoint.IsSetAndValid());
  Text* node = Text::FromNode(aPoint.GetContainer());
  MOZ_ASSERT(node);
  uint32_t pos =
      *aPoint.Offset(RangeBoundary::OffsetFilter::kValidOrInvalidOffsets);
  if (!node) {
    return RangeBoundary{};
  }
  ++pos;
  if (pos < node->Length() &&
      node->GetCharacterDataBuffer()->IsLowSurrogateFollowingHighSurrogateAt(
          pos)) {
    ++pos;
  }
  return {node, pos};
}

/* static */ bool TextDirectiveUtil::WordIsJustWhitespaceOrPunctuation(
    const nsAString& aString, uint32_t aWordBegin, uint32_t aWordEnd) {
  MOZ_ASSERT(aWordEnd <= aString.Length());
  MOZ_ASSERT(aWordBegin < aWordEnd);

  auto word = aString.View().substr(aWordBegin, aWordEnd - aWordBegin);
  return std::all_of(word.cbegin(), word.cend(), [](const char16_t ch) {
    return nsContentUtils::IsHTMLWhitespaceOrNBSP(ch) ||
           mozilla::IsPunctuationForWordSelect(ch);
  });
}

/* static */ bool TextDirectiveUtil::ContainsAtLeastTwoWords(
    const nsAString& aString) {
  uint32_t wordCount = 0;
  for (uint32_t pos = 0; pos < aString.Length();) {
    const auto [wordBegin, wordEnd] = intl::WordBreaker::FindWord(aString, pos);
    if (!WordIsJustWhitespaceOrPunctuation(aString, wordBegin, wordEnd) &&
        ++wordCount == 2) {
      return true;
    }
    pos = wordEnd;
  }
  return false;
}

}  // namespace mozilla::dom
