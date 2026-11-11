/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLEditUtils.h"

#include "AutoClonedRangeArray.h"  // for AutoClonedRangeArray
#include "CSSEditUtils.h"          // for CSSEditUtils
#include "EditAction.h"            // for EditAction
#include "EditorBase.h"            // for EditorBase, EditorType
#include "EditorDOMPoint.h"        // for EditorDOMPoint, etc.
#include "EditorForwards.h"        // for CollectChildrenOptions
#include "EditorUtils.h"           // for EditorUtils
#include "HTMLEditHelpers.h"       // for EditorInlineStyle
#include "WSRunScanner.h"          // for WSRunScanner

#include "mozilla/Assertions.h"  // for MOZ_ASSERT, etc.
#include "mozilla/Attributes.h"
#include "mozilla/StaticPrefs_editor.h"       // for StaticPrefs::editor_
#include "mozilla/RangeUtils.h"               // for RangeUtils
#include "mozilla/dom/CharacterDataBuffer.h"  // for CharacterDataBuffer
#include "mozilla/dom/DocumentInlines.h"      // for GetBodyElement()
#include "mozilla/dom/Element.h"              // for Element, nsINode
#include "mozilla/dom/ElementInlines.h"  // for IsContentEditablePlainTextOnly()
#include "mozilla/dom/HTMLAnchorElement.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/ServoCSSParser.h"  // for ServoCSSParser
#include "mozilla/dom/StaticRange.h"
#include "mozilla/dom/Text.h"  // for Text

#include "nsAString.h"    // for nsAString::IsEmpty
#include "nsAtom.h"       // for nsAtom
#include "nsAttrValue.h"  // nsAttrValue
#include "nsCaseTreatment.h"
#include "nsCOMPtr.h"            // for nsCOMPtr, operator==, etc.
#include "nsComputedDOMStyle.h"  // for nsComputedDOMStyle
#include "nsDebug.h"             // for NS_ASSERTION, etc.
#include "nsElementTable.h"      // for nsHTMLElement
#include "nsError.h"             // for NS_SUCCEEDED
#include "nsGkAtoms.h"           // for nsGkAtoms, nsGkAtoms::a, etc.
#include "nsHTMLTags.h"
#include "nsIContentInlines.h"  // for nsIContent::IsInDesignMode(), etc.
#include "nsIObjectLoadingContent.h"
#include "nsLiteralString.h"     // for NS_LITERAL_STRING
#include "nsNameSpaceManager.h"  // for kNameSpaceID_None
#include "nsPrintfCString.h"     // nsPringfCString
#include "nsString.h"            // for nsAutoString
#include "nsStyledElement.h"
#include "nsStyleStruct.h"  // for StyleDisplay
#include "nsStyleUtil.h"    // for nsStyleUtil
#include "nsTextFrame.h"    // for nsTextFrame

namespace mozilla {

using namespace dom;
using EditorType = EditorBase::EditorType;

template nsIContent* HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
    const EditorDOMPoint&, StopAtBlockSibling, const LeafNodeOptions&,
    BlockInlineCheck, const Element*);
template nsIContent* HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
    const EditorRawDOMPoint&, StopAtBlockSibling, const LeafNodeOptions&,
    BlockInlineCheck, const Element*);
template nsIContent* HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
    const EditorDOMPointInText&, StopAtBlockSibling, const LeafNodeOptions&,
    BlockInlineCheck, const Element*);
template nsIContent* HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
    const EditorRawDOMPointInText&, StopAtBlockSibling, const LeafNodeOptions&,
    BlockInlineCheck, const Element*);

template nsIContent*
HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
    const EditorDOMPoint&, StopAtBlockSibling, const LeafNodeOptions&,
    BlockInlineCheck, const Element*);
template nsIContent*
HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
    const EditorRawDOMPoint&, StopAtBlockSibling, const LeafNodeOptions&,
    BlockInlineCheck, const Element*);
template nsIContent*
HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
    const EditorDOMPointInText&, StopAtBlockSibling, const LeafNodeOptions&,
    BlockInlineCheck, const Element*);
template nsIContent*
HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
    const EditorRawDOMPointInText&, StopAtBlockSibling, const LeafNodeOptions&,
    BlockInlineCheck, const Element*);

template EditorDOMPoint HTMLEditUtils::GetPreviousEditablePoint(
    nsIContent& aContent, const Element* aAncestorLimiter,
    InvisibleWhiteSpaces aInvisibleWhiteSpaces,
    TableBoundary aHowToTreatTableBoundary);
template EditorRawDOMPoint HTMLEditUtils::GetPreviousEditablePoint(
    nsIContent& aContent, const Element* aAncestorLimiter,
    InvisibleWhiteSpaces aInvisibleWhiteSpaces,
    TableBoundary aHowToTreatTableBoundary);
template EditorDOMPoint HTMLEditUtils::GetNextEditablePoint(
    nsIContent& aContent, const Element* aAncestorLimiter,
    InvisibleWhiteSpaces aInvisibleWhiteSpaces,
    TableBoundary aHowToTreatTableBoundary);
template EditorRawDOMPoint HTMLEditUtils::GetNextEditablePoint(
    nsIContent& aContent, const Element* aAncestorLimiter,
    InvisibleWhiteSpaces aInvisibleWhiteSpaces,
    TableBoundary aHowToTreatTableBoundary);

template EditorDOMPoint HTMLEditUtils::LineRequiresPaddingLineBreakToBeVisible(
    const EditorDOMPoint& aPoint, const Element& aEditingHost);
template EditorDOMPoint HTMLEditUtils::LineRequiresPaddingLineBreakToBeVisible(
    const EditorRawDOMPoint& aPoint, const Element& aEditingHost);
template EditorDOMPoint HTMLEditUtils::LineRequiresPaddingLineBreakToBeVisible(
    const EditorDOMPointInText& aPoint, const Element& aEditingHost);
template EditorDOMPoint HTMLEditUtils::LineRequiresPaddingLineBreakToBeVisible(
    const EditorRawDOMPointInText& aPoint, const Element& aEditingHost);

template nsIContent* HTMLEditUtils::GetContentToPreserveInlineStyles(
    const EditorDOMPoint& aPoint, const Element& aEditingHost);
template nsIContent* HTMLEditUtils::GetContentToPreserveInlineStyles(
    const EditorRawDOMPoint& aPoint, const Element& aEditingHost);

template EditorDOMPoint HTMLEditUtils::GetBetterInsertionPointFor(
    const nsIContent& aContentToInsert, const EditorDOMPoint& aPointToInsert);
template EditorRawDOMPoint HTMLEditUtils::GetBetterInsertionPointFor(
    const nsIContent& aContentToInsert,
    const EditorRawDOMPoint& aPointToInsert);
template EditorDOMPoint HTMLEditUtils::GetBetterInsertionPointFor(
    const nsIContent& aContentToInsert,
    const EditorRawDOMPoint& aPointToInsert);
template EditorRawDOMPoint HTMLEditUtils::GetBetterInsertionPointFor(
    const nsIContent& aContentToInsert, const EditorDOMPoint& aPointToInsert);

template EditorDOMPoint HTMLEditUtils::GetBetterCaretPositionToInsertText(
    const EditorDOMPoint& aPoint);
template EditorDOMPoint HTMLEditUtils::GetBetterCaretPositionToInsertText(
    const EditorRawDOMPoint& aPoint);
template EditorRawDOMPoint HTMLEditUtils::GetBetterCaretPositionToInsertText(
    const EditorDOMPoint& aPoint);
template EditorRawDOMPoint HTMLEditUtils::GetBetterCaretPositionToInsertText(
    const EditorRawDOMPoint& aPoint);

template Result<EditorDOMPoint, nsresult>
HTMLEditUtils::ComputePointToPutCaretInElementIfOutside(
    const Element& aElement, const EditorDOMPoint& aCurrentPoint);
template Result<EditorRawDOMPoint, nsresult>
HTMLEditUtils::ComputePointToPutCaretInElementIfOutside(
    const Element& aElement, const EditorDOMPoint& aCurrentPoint);
template Result<EditorDOMPoint, nsresult>
HTMLEditUtils::ComputePointToPutCaretInElementIfOutside(
    const Element& aElement, const EditorRawDOMPoint& aCurrentPoint);
template Result<EditorRawDOMPoint, nsresult>
HTMLEditUtils::ComputePointToPutCaretInElementIfOutside(
    const Element& aElement, const EditorRawDOMPoint& aCurrentPoint);

template Maybe<EditorLineBreak>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorDOMPoint&, const Element&);
template Maybe<EditorRawLineBreak>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorDOMPoint&, const Element&);
template Maybe<EditorLineBreak>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorRawDOMPoint&, const Element&);
template Maybe<EditorRawLineBreak>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorRawDOMPoint&, const Element&);
template Maybe<EditorLineBreak>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorDOMPointInText&, const Element&);
template Maybe<EditorRawLineBreak>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorDOMPointInText&, const Element&);
template Maybe<EditorLineBreak>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorRawDOMPointInText&, const Element&);
template Maybe<EditorRawLineBreak>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorRawDOMPointInText&, const Element&);

template bool HTMLEditUtils::IsSameCSSColorValue(const nsAString& aColorA,
                                                 const nsAString& aColorB);
template bool HTMLEditUtils::IsSameCSSColorValue(const nsACString& aColorA,
                                                 const nsACString& aColorB);

template bool HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
    const EditorDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
    const EditorRawDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
    const EditorDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
    const EditorRawDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);

template bool
HTMLEditUtils::IsPreformattedLineBreakFollowedByCurrentBlockBoundary(
    const EditorDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowedByCurrentBlockBoundary(
    const EditorRawDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowedByCurrentBlockBoundary(
    const EditorDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowedByCurrentBlockBoundary(
    const EditorRawDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);

template bool
HTMLEditUtils::IsPreformattedLineBreakFollowingCurrentBlockBoundary(
    const EditorDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowingCurrentBlockBoundary(
    const EditorRawDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowingCurrentBlockBoundary(
    const EditorDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowingCurrentBlockBoundary(
    const EditorRawDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);

template bool
HTMLEditUtils::IsPreformattedLineBreakFollowedByOtherBlockBoundary(
    const EditorDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowedByOtherBlockBoundary(
    const EditorRawDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowedByOtherBlockBoundary(
    const EditorDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool
HTMLEditUtils::IsPreformattedLineBreakFollowedByOtherBlockBoundary(
    const EditorRawDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);

template bool HTMLEditUtils::IsPreformattedLineBreakFollowedByLineBoundary(
    const EditorDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowedByLineBoundary(
    const EditorRawDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowedByLineBoundary(
    const EditorDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowedByLineBoundary(
    const EditorRawDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);

template bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBoundary(
    const EditorDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBoundary(
    const EditorRawDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBoundary(
    const EditorDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBoundary(
    const EditorRawDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*,
    Element**);

template bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBreak(
    const EditorDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBreak(
    const EditorRawDOMPoint&, SkipWhiteSpaceStyleCheck, const Element*);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBreak(
    const EditorDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*);
template bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBreak(
    const EditorRawDOMPointInText&, SkipWhiteSpaceStyleCheck, const Element*);

template bool HTMLEditUtils::IsUnnecessaryPreformattedLineBreak(
    const EditorDOMPoint&, TreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsUnnecessaryPreformattedLineBreak(
    const EditorRawDOMPoint&, TreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsUnnecessaryPreformattedLineBreak(
    const EditorDOMPointInText&, TreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsUnnecessaryPreformattedLineBreak(
    const EditorRawDOMPointInText&, TreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck, const Element*, Element**);

template bool HTMLEditUtils::IsSignificantPreformattedLineBreak(
    const EditorDOMPoint&, TreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsSignificantPreformattedLineBreak(
    const EditorRawDOMPoint&, TreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsSignificantPreformattedLineBreak(
    const EditorDOMPointInText&, TreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck, const Element*, Element**);
template bool HTMLEditUtils::IsSignificantPreformattedLineBreak(
    const EditorRawDOMPointInText&, TreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck, const Element*, Element**);

template WSScanResult
HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
    const EditorDOMPoint&, TreatAsUnnecessaryLineBreak, const Element&,
    const Element*);
template WSScanResult
HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
    const EditorRawDOMPoint&, TreatAsUnnecessaryLineBreak, const Element&,
    const Element*);
template WSScanResult
HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
    const EditorDOMPointInText&, TreatAsUnnecessaryLineBreak, const Element&,
    const Element*);
template WSScanResult
HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
    const EditorRawDOMPointInText&, TreatAsUnnecessaryLineBreak, const Element&,
    const Element*);

template Maybe<EditorLineBreak> HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(
    const EditorDOMPoint&, const Element*);
template Maybe<EditorLineBreak> HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(
    const EditorRawDOMPoint&, const Element*);
template Maybe<EditorLineBreak> HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(
    const EditorDOMPointInText&, const Element*);
template Maybe<EditorLineBreak> HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(
    const EditorRawDOMPointInText&, const Element*);
template Maybe<EditorRawLineBreak>
HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(const EditorDOMPoint&,
                                                const Element*);
template Maybe<EditorRawLineBreak>
HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(const EditorRawDOMPoint&,
                                                const Element*);
template Maybe<EditorRawLineBreak>
HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(const EditorDOMPointInText&,
                                                const Element*);
template Maybe<EditorRawLineBreak>
HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(const EditorRawDOMPointInText&,
                                                const Element*);

template bool HTMLEditUtils::PointIsImmediatelyBeforeCurrentBlockBoundary(
    const EditorDOMPoint& aPoint,
    IgnoreInvisibleLineBreak aIgnoreInvisibleLineBreak);
template bool HTMLEditUtils::PointIsImmediatelyBeforeCurrentBlockBoundary(
    const EditorRawDOMPoint& aPoint,
    IgnoreInvisibleLineBreak aIgnoreInvisibleLineBreak);
template bool HTMLEditUtils::PointIsImmediatelyBeforeCurrentBlockBoundary(
    const EditorDOMPointInText& aPoint,
    IgnoreInvisibleLineBreak aIgnoreInvisibleLineBreak);
template bool HTMLEditUtils::PointIsImmediatelyBeforeCurrentBlockBoundary(
    const EditorRawDOMPointInText& aPoint,
    IgnoreInvisibleLineBreak aIgnoreInvisibleLineBreak);

bool HTMLEditUtils::ElementIsEditableRoot(const Element& aElement) {
  MOZ_ASSERT(!aElement.IsInNativeAnonymousSubtree());
  if (NS_WARN_IF(!aElement.IsEditable()) ||
      NS_WARN_IF(!aElement.IsInComposedDoc())) {
    return false;
  }
  return !aElement.GetParent() ||                      // root element
         !aElement.GetParent()->IsEditable() ||        // editing host
         aElement.OwnerDoc()->GetBody() == &aElement;  // the <body>
}

bool HTMLEditUtils::CanContentsBeJoined(const nsIContent& aLeftContent,
                                        const nsIContent& aRightContent) {
  if (aLeftContent.NodeInfo()->NameAtom() !=
      aRightContent.NodeInfo()->NameAtom()) {
    return false;
  }

  if (!aLeftContent.IsElement()) {
    return true;  // can join text nodes, etc
  }
  MOZ_ASSERT(aRightContent.IsElement());

  if (aLeftContent.NodeInfo()->NameAtom() == nsGkAtoms::font) {
    const nsAttrValue* const leftSize =
        aLeftContent.AsElement()->GetParsedAttr(nsGkAtoms::size);
    const nsAttrValue* const rightSize =
        aRightContent.AsElement()->GetParsedAttr(nsGkAtoms::size);
    if (!leftSize ^ !rightSize || (leftSize && !leftSize->Equals(*rightSize))) {
      return false;
    }

    const nsAttrValue* const leftColor =
        aLeftContent.AsElement()->GetParsedAttr(nsGkAtoms::color);
    const nsAttrValue* const rightColor =
        aRightContent.AsElement()->GetParsedAttr(nsGkAtoms::color);
    if (!leftColor ^ !rightColor ||
        (leftColor && !leftColor->Equals(*rightColor))) {
      return false;
    }

    const nsAttrValue* const leftFace =
        aLeftContent.AsElement()->GetParsedAttr(nsGkAtoms::face);
    const nsAttrValue* const rightFace =
        aRightContent.AsElement()->GetParsedAttr(nsGkAtoms::face);
    if (!leftFace ^ !rightFace || (leftFace && !leftFace->Equals(*rightFace))) {
      return false;
    }
  }
  nsStyledElement* leftStyledElement =
      nsStyledElement::FromNode(const_cast<nsIContent*>(&aLeftContent));
  if (!leftStyledElement) {
    return false;
  }
  nsStyledElement* rightStyledElement =
      nsStyledElement::FromNode(const_cast<nsIContent*>(&aRightContent));
  if (!rightStyledElement) {
    return false;
  }
  return CSSEditUtils::DoStyledElementsHaveSameStyle(*leftStyledElement,
                                                     *rightStyledElement);
}

static bool IsHTMLBlockElementByDefault(const nsIContent& aContent) {
  if (!aContent.IsHTMLElement()) {
    return false;
  }
  if (aContent.IsHTMLElement(nsGkAtoms::br)) {  // shortcut for TextEditor
    MOZ_ASSERT(!nsHTMLElement::IsBlock(
        aContent.NodeInfo()->HTMLTag().valueOr(eHTMLTag_userdefined)));
    return false;
  }
  // We want to treat these as block nodes even though nsHTMLElement says
  // they're not.
  if (aContent.IsAnyOfHTMLElements(
          nsGkAtoms::body, nsGkAtoms::head, nsGkAtoms::tbody, nsGkAtoms::thead,
          nsGkAtoms::tfoot, nsGkAtoms::tr, nsGkAtoms::th, nsGkAtoms::td,
          nsGkAtoms::dt, nsGkAtoms::dd)) {
    return true;
  }

  return nsHTMLElement::IsBlock(
      aContent.NodeInfo()->HTMLTag().valueOr(eHTMLTag_userdefined));
}

bool HTMLEditUtils::IsBlockElement(const nsIContent& aContent,
                                   BlockInlineCheck aBlockInlineCheck) {
  MOZ_ASSERT(aBlockInlineCheck != BlockInlineCheck::Unused);
  MOZ_ASSERT(aBlockInlineCheck != BlockInlineCheck::Auto);

  if (MOZ_UNLIKELY(!aContent.IsElement())) {
    // FIXME: If aContent is a visible `Text` and a flex/grid item, we should
    // treat it as block.
    return false;
  }
  // If it's a <br>, we should always treat it as an inline element because
  // its preceding collapse white-spaces and another <br> works same as usual
  // even if you set its style to `display:block`.
  if (aContent.IsHTMLElement(nsGkAtoms::br)) {
    return false;
  }
  if (aBlockInlineCheck == BlockInlineCheck::UseHTMLDefaultStyle) {
    return IsHTMLBlockElementByDefault(aContent);
  }
  // Let's treat the document element and the body element is a block to avoid
  // complicated things which may be detected by fuzzing.
  if (aContent.OwnerDoc()->GetDocumentElement() == &aContent ||
      (aContent.IsHTMLElement(nsGkAtoms::body) &&
       aContent.OwnerDoc()->GetBodyElement() == &aContent)) {
    return true;
  }
  RefPtr<const ComputedStyle> elementStyle =
      nsComputedDOMStyle::GetComputedStyleNoFlush(aContent.AsElement());
  if (MOZ_UNLIKELY(!elementStyle)) {  // If aContent is not in the composed tree
    return IsHTMLBlockElementByDefault(aContent);
  }
  const nsStyleDisplay* styleDisplay = elementStyle->StyleDisplay();
  if (MOZ_UNLIKELY(styleDisplay->mDisplay == StyleDisplay::None)) {
    // Typically, we should not keep handling editing in invisible nodes, but if
    // we reach here, let's fallback to the default style for protecting the
    // structure as far as possible.
    return IsHTMLBlockElementByDefault(aContent);
  }
  // If the outside is not inline, treat it as block.
  if (!styleDisplay->IsInlineOutsideStyle()) {
    return true;
  }
  // Special case.  If aContent is a grid or flex item, we want to treat it as a
  // block to handle it with the general paths.
  if (HTMLEditUtils::ParentElementIsGridOrFlexContainer(aContent)) {
    return true;
  }
  // If we're checking display-inside, inline-block, etc should be a block too.
  return aBlockInlineCheck == BlockInlineCheck::UseComputedDisplayStyle &&
         styleDisplay->DisplayInside() == StyleDisplayInside::FlowRoot &&
         // Treat widgets as inline since they won't hide collapsible
         // white-spaces around them.
         styleDisplay->EffectiveAppearance() == StyleAppearance::None;
}

bool HTMLEditUtils::IsInlineContent(const nsIContent& aContent,
                                    BlockInlineCheck aBlockInlineCheck) {
  MOZ_ASSERT(aBlockInlineCheck != BlockInlineCheck::Unused);
  MOZ_ASSERT(aBlockInlineCheck != BlockInlineCheck::Auto);

  if (!aContent.IsElement()) {
    // FIXME: If aContent is a visible `Text` and a flex/grid item, we should
    // treat it as block.
    return true;
  }
  // If it's a <br>, we should always treat it as an inline element because
  // its preceding collapse white-spaces and another <br> works same as usual
  // even if you set its style to `display:block`.
  if (aContent.IsHTMLElement(nsGkAtoms::br)) {
    return true;
  }
  if (aBlockInlineCheck == BlockInlineCheck::UseHTMLDefaultStyle) {
    return !IsHTMLBlockElementByDefault(aContent);
  }
  // Let's treat the document element and the body element is a block to avoid
  // complicated things which may be detected by fuzzing.
  if (aContent.OwnerDoc()->GetDocumentElement() == &aContent ||
      (aContent.IsHTMLElement(nsGkAtoms::body) &&
       aContent.OwnerDoc()->GetBodyElement() == &aContent)) {
    return false;
  }
  RefPtr<const ComputedStyle> elementStyle =
      nsComputedDOMStyle::GetComputedStyleNoFlush(aContent.AsElement());
  if (MOZ_UNLIKELY(!elementStyle)) {  // If aContent is not in the composed tree
    return !IsHTMLBlockElementByDefault(aContent);
  }
  const nsStyleDisplay* styleDisplay = elementStyle->StyleDisplay();
  if (MOZ_UNLIKELY(styleDisplay->mDisplay == StyleDisplay::None)) {
    // Similar to IsBlockElement, let's fallback to refer the default style.
    // Note that if you change here, you may need to check the parent element
    // style if aContent.
    return !IsHTMLBlockElementByDefault(aContent);
  }
  // Special case.  If aContent is a grid or flex item, we want to treat it as a
  // block to handle it with the general paths.
  if (HTMLEditUtils::ParentElementIsGridOrFlexContainer(aContent)) {
    return false;
  }
  // Different block IsBlockElement, when the display-outside is inline, it's
  // simply an inline element.
  return styleDisplay->IsInlineOutsideStyle();
}

bool HTMLEditUtils::ParentElementIsGridOrFlexContainer(
    const nsIContent& aMaybeFlexOrGridItemContent) {
  if (!aMaybeFlexOrGridItemContent.IsElement()) {
    if (!aMaybeFlexOrGridItemContent.IsText() ||
        !aMaybeFlexOrGridItemContent.AsText()->TextDataLength()) {
      return false;
    }
    // FIXME: If aMaybeFlexOrGridItemContent has only collapsible white-spaces
    // and next to a block boundary, it's invisible and shouldn't be a flex/grid
    // item.  However, scanning block boundary requires to call this method.
  }
  Element* const parentElement = aMaybeFlexOrGridItemContent.GetParentElement();
  // Editable state does not affect to elements across shadow DOM boundaries.
  // Therefore, we don't need to treat aElement as an flex item nor a grid item
  // as so if aElement is a root element of a shadow DOM unless we'll support
  // inline editing host support better (all browsers do not handle surrounding
  // content of the inline editing host strictly, e.g., when inserting a
  // collapsible white-space at start or end of it).
  if (MOZ_UNLIKELY(!parentElement)) {
    return false;
  }
  // We should consider whether the element is a flex item or a grid item
  // without nsIFrame since we don't want to refresh the layout while
  // `HTMLEditor` handles an action to avoid to run script.
  const RefPtr<const ComputedStyle> elementStyle =
      nsComputedDOMStyle::GetComputedStyleNoFlush(
          aMaybeFlexOrGridItemContent.IsElement()
              ? aMaybeFlexOrGridItemContent.AsElement()
              : parentElement);
  if (MOZ_UNLIKELY(!elementStyle)) {
    return false;
  }
  const nsStyleDisplay* styleDisplay = elementStyle->StyleDisplay();
  if (MOZ_UNLIKELY(styleDisplay->mDisplay == StyleDisplay::None)) {
    return false;
  }
  const RefPtr<const ComputedStyle> parentElementStyle =
      aMaybeFlexOrGridItemContent.IsElement()
          ? nsComputedDOMStyle::GetComputedStyleNoFlush(parentElement)
          : elementStyle;
  if (MOZ_UNLIKELY(!parentElementStyle)) {
    return false;
  }
  const auto parentDisplayInside =
      parentElementStyle->StyleDisplay()->DisplayInside();
  return parentDisplayInside == StyleDisplayInside::Flex ||
         parentDisplayInside == StyleDisplayInside::Grid;
}

bool HTMLEditUtils::IsFlexOrGridItem(const nsIContent& aContent) {
  if (!HTMLEditUtils::ParentElementIsGridOrFlexContainer(aContent)) {
    return false;
  }
  // Note that if parent element's `display` is `contents`, the
  // `display-outside` style of aElement may not be block.  However, even in
  // such case, HTMLEditUtils::IsBlockElement() should return true.
  MOZ_ASSERT_IF(aContent.IsElement(),
                HTMLEditUtils::IsBlockElement(
                    *aContent.AsElement(),
                    BlockInlineCheck::UseComputedDisplayOutsideStyle));
  return true;
}

bool HTMLEditUtils::IsInclusiveAncestorCSSDisplayNone(
    const nsIContent& aContent,
    const nsIContent* aAncestorLimiter /* = nullptr */) {
  if (NS_WARN_IF(!aContent.IsInComposedDoc())) {
    return true;
  }
  for (const Element* element :
       aContent.InclusiveFlatTreeAncestorsOfType<Element>()) {
    RefPtr<const ComputedStyle> elementStyle =
        nsComputedDOMStyle::GetComputedStyleNoFlush(element);
    if (MOZ_LIKELY(elementStyle)) {
      const nsStyleDisplay* styleDisplay = elementStyle->StyleDisplay();
      if (MOZ_UNLIKELY(styleDisplay->mDisplay == StyleDisplay::None)) {
        return true;
      }
    }
    if (element == aAncestorLimiter) {
      break;
    }
  }
  return false;
}

bool HTMLEditUtils::IsVisibleElementEvenIfLeafNode(const nsIContent& aContent) {
  if (!aContent.IsElement()) {
    return false;
  }
  // Assume non-HTML element is visible.
  if (!aContent.IsHTMLElement()) {
    return true;
  }
  if (HTMLEditUtils::IsBlockElement(
          aContent, BlockInlineCheck::UseComputedDisplayStyle)) {
    return true;
  }
  // <br> element may not have a frame, but it always affects surrounding
  // content.  Therefore, it should be treated as visible.  The others which are
  // checked here are replace elements which provide something visible content.
  if (aContent.IsAnyOfHTMLElements(nsGkAtoms::applet, nsGkAtoms::br,
                                   nsGkAtoms::iframe, nsGkAtoms::img,
                                   nsGkAtoms::meter, nsGkAtoms::progress,
                                   nsGkAtoms::select, nsGkAtoms::textarea)) {
    return true;
  }
  if (const auto* inputElement = HTMLInputElement::FromNode(aContent)) {
    return inputElement->ControlType() != FormControlType::InputHidden;
  }
  if (nsIFrame* const primaryFrame = aContent.GetPrimaryFrame()) {
    // If the frame is not dirty or non-inline container frame, we can trust
    // whether the frame is empty or not.
    if (!primaryFrame->IsSubtreeDirty() || !primaryFrame->IsInlineFrame()) {
      return !primaryFrame->GetSize().IsEmpty();
    }
    // Otherwise, the inner content may have been changed by the editor or JS.
    // Let's treat it's visible only when it has non-zero border or padding.
    return !primaryFrame->IsSelfEmpty();
  }
  // If aContent does not have a primary frame, it may be inserted to the
  // document and has not been flushed the pending notifications.  Then, we
  // cannot know the actual style so that let's assume it's invisible.
  return false;
}

bool HTMLEditUtils::IsInlineStyleElement(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(
      nsGkAtoms::b, nsGkAtoms::i, nsGkAtoms::u, nsGkAtoms::tt, nsGkAtoms::s,
      nsGkAtoms::strike, nsGkAtoms::big, nsGkAtoms::small, nsGkAtoms::sub,
      nsGkAtoms::sup, nsGkAtoms::font);
}

bool HTMLEditUtils::IsDisplayOutsideInline(const Element& aElement) {
  RefPtr<const ComputedStyle> elementStyle =
      nsComputedDOMStyle::GetComputedStyleNoFlush(&aElement);
  if (!elementStyle) {
    return false;
  }
  return elementStyle->StyleDisplay()->DisplayOutside() ==
         StyleDisplayOutside::Inline;
}

bool HTMLEditUtils::IsDisplayInsideFlowRoot(const Element& aElement) {
  RefPtr<const ComputedStyle> elementStyle =
      nsComputedDOMStyle::GetComputedStyleNoFlush(&aElement);
  if (!elementStyle) {
    return false;
  }
  return elementStyle->StyleDisplay()->DisplayInside() ==
         StyleDisplayInside::FlowRoot;
}

bool HTMLEditUtils::IsRemovableInlineStyleElement(Element& aElement) {
  if (!aElement.IsHTMLElement()) {
    return false;
  }
  // https://w3c.github.io/editing/execCommand.html#removeformat-candidate
  if (aElement.IsAnyOfHTMLElements(
          nsGkAtoms::abbr,  // Chrome ignores, but does not make sense.
          nsGkAtoms::acronym, nsGkAtoms::b,
          nsGkAtoms::bdi,  // Chrome ignores, but does not make sense.
          nsGkAtoms::bdo, nsGkAtoms::big, nsGkAtoms::cite, nsGkAtoms::code,
          // nsGkAtoms::del, Chrome ignores, but does not make sense but
          // execCommand unofficial draft excludes this.  Spec issue:
          // https://github.com/w3c/editing/issues/192
          nsGkAtoms::dfn, nsGkAtoms::em, nsGkAtoms::font, nsGkAtoms::i,
          nsGkAtoms::ins, nsGkAtoms::kbd,
          nsGkAtoms::mark,  // Chrome ignores, but does not make sense.
          nsGkAtoms::nobr, nsGkAtoms::q, nsGkAtoms::s, nsGkAtoms::samp,
          nsGkAtoms::small, nsGkAtoms::span, nsGkAtoms::strike,
          nsGkAtoms::strong, nsGkAtoms::sub, nsGkAtoms::sup, nsGkAtoms::tt,
          nsGkAtoms::u, nsGkAtoms::var)) {
    return true;
  }
  // If it's a <blink> element, we can remove it.
  nsAutoString tagName;
  aElement.GetTagName(tagName);
  return tagName.LowerCaseEqualsASCII("blink");
}

bool HTMLEditUtils::IsOutdentable(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(
      nsGkAtoms::ul, nsGkAtoms::ol, nsGkAtoms::dl, nsGkAtoms::li, nsGkAtoms::dd,
      nsGkAtoms::dt, nsGkAtoms::blockquote);
}

bool HTMLEditUtils::IsHeadingElement(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(nsGkAtoms::h1, nsGkAtoms::h2,
                                      nsGkAtoms::h3, nsGkAtoms::h4,
                                      nsGkAtoms::h5, nsGkAtoms::h6);
}

bool HTMLEditUtils::IsListItemElement(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(nsGkAtoms::li, nsGkAtoms::dd,
                                      nsGkAtoms::dt);
}

bool HTMLEditUtils::IsAnyTableElementExceptColumnElement(
    const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(
      nsGkAtoms::table, nsGkAtoms::tr, nsGkAtoms::td, nsGkAtoms::th,
      nsGkAtoms::thead, nsGkAtoms::tfoot, nsGkAtoms::tbody, nsGkAtoms::caption);
}

bool HTMLEditUtils::IsAnyTableElementExceptTableElementAndColumElement(
    const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(
      nsGkAtoms::tr, nsGkAtoms::td, nsGkAtoms::th, nsGkAtoms::thead,
      nsGkAtoms::tfoot, nsGkAtoms::tbody, nsGkAtoms::caption);
}

bool HTMLEditUtils::IsTableRowElement(const nsIContent& aContent) {
  return aContent.IsHTMLElement(nsGkAtoms::tr);
}

bool HTMLEditUtils::IsTableCellElement(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(nsGkAtoms::td, nsGkAtoms::th);
}

bool HTMLEditUtils::IsTableCellOrCaptionElement(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(nsGkAtoms::td, nsGkAtoms::th,
                                      nsGkAtoms::caption);
}

bool HTMLEditUtils::IsListElement(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(nsGkAtoms::ul, nsGkAtoms::ol,
                                      nsGkAtoms::dl);
}

bool HTMLEditUtils::IsImageElement(const nsIContent& aContent) {
  // XXX How about <object> and <picture>?
  return aContent.IsHTMLElement(nsGkAtoms::img);
}

bool HTMLEditUtils::IsHyperlinkElement(const nsIContent& aContent) {
  const dom::HTMLAnchorElement* const anchor =
      dom::HTMLAnchorElement::FromNode(aContent);
  if (!anchor) {
    return false;
  }
  // XXX Isn't it enough to check whether the `href` value is empty?
  nsAutoCString tmpText;
  anchor->GetHref(tmpText);
  return !tmpText.IsEmpty();
}

bool HTMLEditUtils::IsNamedAnchorElement(const nsIContent& aContent) {
  const dom::HTMLAnchorElement* const anchor =
      dom::HTMLAnchorElement::FromNode(aContent);
  if (!anchor) {
    return false;
  }
  return anchor->HasName();
}

bool HTMLEditUtils::IsMozDivElement(const nsIContent& aContent) {
  return aContent.IsHTMLElement(nsGkAtoms::div) &&
         aContent.AsElement()->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                                           u"_moz"_ns, eIgnoreCase);
}

bool HTMLEditUtils::IsMailCiteElement(const Element& aElement) {
  // don't ask me why, but our html mailcites are id'd by "type=cite"...
  if (aElement.AttrValueIs(kNameSpaceID_None, nsGkAtoms::type, u"cite"_ns,
                           eIgnoreCase)) {
    return true;
  }

  // ... but our plaintext mailcites by "_moz_quote=true".  go figure.
  if (aElement.AttrValueIs(kNameSpaceID_None, nsGkAtoms::mozquote, u"true"_ns,
                           eIgnoreCase)) {
    return true;
  }

  return false;
}

bool HTMLEditUtils::IsReplacedElement(const Element& aElement) {
  if (!aElement.IsHTMLElement()) {
    // FIXME: Well known SVG, MathML elements should be tested here.
    return false;
  }
  if (aElement.IsHTMLElement(nsGkAtoms::input)) {
    return !aElement.AttrValueIs(kNameSpaceID_None, nsGkAtoms::type,
                                 nsGkAtoms::hidden, eIgnoreCase);
  }
  // <object> is a special element, it shows its subtree when it does not load
  // its content.
  if (aElement.IsHTMLElement(nsGkAtoms::object)) {
    const nsCOMPtr<nsIObjectLoadingContent> objectLoadingContent =
        do_QueryInterface(const_cast<Element*>(&aElement));
    uint32_t displayedType = nsIObjectLoadingContent::TYPE_FALLBACK;
    if (MOZ_LIKELY(objectLoadingContent)) {
      objectLoadingContent->GetDisplayedType(&displayedType);
    }
    return displayedType != nsIObjectLoadingContent::TYPE_FALLBACK;
  }
  return aElement.IsAnyOfHTMLElements(
      nsGkAtoms::audio,
      // In strictly speaking, <br> is not a replaced element, but treating it
      // as a replaced element makes HTMLEditor and its peers simpler.
      nsGkAtoms::br, nsGkAtoms::button, nsGkAtoms::canvas, nsGkAtoms::embed,
      nsGkAtoms::iframe, nsGkAtoms::img, nsGkAtoms::meter,
      // <optgroup> and <option> are not replaced element actually but they
      // are treated as so for the compatibility with Chrome.
      // XXX I wonder if we can treat them as so only when they are in
      // <select>.
      nsGkAtoms::optgroup, nsGkAtoms::option, nsGkAtoms::progress,
      nsGkAtoms::select, nsGkAtoms::textarea, nsGkAtoms::video);
}

bool HTMLEditUtils::IsAlignAttrSupported(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(
      nsGkAtoms::hr, nsGkAtoms::table, nsGkAtoms::tbody, nsGkAtoms::tfoot,
      nsGkAtoms::thead, nsGkAtoms::tr, nsGkAtoms::td, nsGkAtoms::th,
      nsGkAtoms::div, nsGkAtoms::p, nsGkAtoms::h1, nsGkAtoms::h2, nsGkAtoms::h3,
      nsGkAtoms::h4, nsGkAtoms::h5, nsGkAtoms::h6);
}

bool HTMLEditUtils::IsVisibleTextNode(
    const Text& aText, TreatInvisibleLineBreakAs aTreatInvisibleLineBreakAs) {
  if (!aText.TextDataLength()) {
    return false;
  }

  EditorRawDOMPointInText atPreformattedLineBreak;
  const Maybe<uint32_t> visibleCharOffset =
      HTMLEditUtils::GetInclusiveNextNonCollapsibleCharOffset(
          EditorDOMPointInText(&aText, 0));
  if (visibleCharOffset.isSome()) {
    atPreformattedLineBreak.Set(&aText, *visibleCharOffset);
    if (aTreatInvisibleLineBreakAs == TreatInvisibleLineBreakAs::Visible ||
        !atPreformattedLineBreak.IsCharNewLine()) {
      return true;
    }
    // If aText has a visible character other than the first preformatted
    // linefeed, it's visible.
    Maybe<EditorRawLineBreak> preformattedLineBreak =
        EditorRawLineBreak::CreateIfTextHasOnlyOneAndNoOtherVisibleCharacters(
            aText);
    if (!preformattedLineBreak) {
      return true;  // not only a preformatted linefeed so that it's visible.
    }
    if (!preformattedLineBreak->IsFollowedByBlockBoundary()) {
      return true;  // a line break before a visible content.
    }
    // If the line break follows a line boundary, it makes an empty line before
    // the block boundary.
    return preformattedLineBreak->IsFollowingLineBoundary();
  }

  // Now, all characters in aText is collapsible white-spaces.  The node is
  // invisible if next to block boundary.
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          {}, EditorRawDOMPoint::After(aText));
  if (followingThing.ReachedBlockBoundary()) {
    // If collapsible white-spaces are followed by a block boundary, they are
    // invisible.
    return false;
  }
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          {}, EditorRawDOMPoint(&aText));
  if (precedingThing.ReachedBlockBoundary()) {
    // If collapsible white-spaces follows a block boundary, they are
    // invisible.
    return false;
  }
  // Backward compatibility for the legacy document created by us.
  // Putting a <br> after a collapsed white-space made it visible if and only if
  // it follows a visible thing.
  // FIXME: We should stop doing this after fixing bug 1951041 even if we'd
  // break the backward compatibility.
  if (followingThing.ReachedBRElement() &&
      !precedingThing.ReachedLineBoundary()) {
    return true;
  }
  // If aText is between line breaks or block boundaries, it's invisible.
  if (followingThing.ReachedLineBoundary() &&
      precedingThing.ReachedLineBoundary()) {
    return false;
  }
  // Otherwise, the white-spaces should be collapsed to a visible white-space.
  // FIXME: If aText has only collapsible white-spaces and a preceding `Text`
  // ends with collapsible white-spaces, all of the invisible white-spaces in
  // aText is merged into the preceding white-spaces.  Therefore, in such case,
  // we need to treat aText is invisible.
  return true;
}

bool HTMLEditUtils::IsInVisibleTextFrames(nsPresContext* aPresContext,
                                          const Text& aText) {
  // TODO(dholbert): aPresContext is now unused; maybe we can remove it, here
  // and in IsEmptyNode?  We do use it as a signal (implicitly here,
  // more-explicitly in IsEmptyNode) that we are in a "SafeToAskLayout" case...
  // If/when we remove it, we should be sure we're not losing that signal of
  // strictness, since this function here does absolutely need to query layout.
  MOZ_ASSERT(aPresContext);

  if (!aText.TextDataLength()) {
    return false;
  }

  nsTextFrame* textFrame = do_QueryFrame(aText.GetPrimaryFrame());
  if (!textFrame) {
    return false;
  }

  return textFrame->HasVisibleText();
}

bool HTMLEditUtils::IsBRElementFollowedByBlockBoundary(
    const dom::HTMLBRElement& aBRElement,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we cant treat it's not
          // followed by a block boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          EditorRawDOMPoint::After(aBRElement), aAncestorLimiter);
  if (!followingThing.ReachedBlockBoundary()) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ElementPtr();
  }
  return true;
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
    const EditorDOMPointType& aPoint,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNewLine()) {
    return false;
  }
  if (aSkipWhiteSpaceStyleCheck == SkipWhiteSpaceStyleCheck::No &&
      !EditorUtils::IsNewLinePreformatted(
          *aPoint.template ContainerAs<Text>())) {
    return false;
  }
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we can treat it's not
          // followed by a block boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          aPoint.template NextPoint<EditorRawDOMPoint>(), aAncestorLimiter);
  if (!followingThing.ReachedBlockBoundary()) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ElementPtr();
  }
  return true;
}

bool HTMLEditUtils::IsBRElementFollowedByCurrentBlockBoundary(
    const dom::HTMLBRElement& aBRElement,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we cant treat it's not
          // followed by a block boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          EditorRawDOMPoint::After(aBRElement), aAncestorLimiter);
  if (!followingThing.ReachedCurrentBlockBoundary()) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ElementPtr();
  }
  return true;
}

bool HTMLEditUtils::IsBRElementFollowedByOtherBlockBoundary(
    const dom::HTMLBRElement& aBRElement,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we cant treat it's not
          // followed by a block boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          EditorRawDOMPoint::After(aBRElement), aAncestorLimiter);
  if (!followingThing.ReachedOtherBlockElement()) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ElementPtr();
  }
  return true;
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsPreformattedLineBreakFollowedByCurrentBlockBoundary(
    const EditorDOMPointType& aPoint,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNewLine()) {
    return false;
  }
  if (aSkipWhiteSpaceStyleCheck == SkipWhiteSpaceStyleCheck::No &&
      !EditorUtils::IsNewLinePreformatted(
          *aPoint.template ContainerAs<Text>())) {
    return false;
  }
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we can treat it's not
          // followed by a block boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          aPoint.template NextPoint<EditorRawDOMPoint>(), aAncestorLimiter);
  if (!followingThing.ReachedCurrentBlockBoundary()) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ElementPtr();
  }
  return true;
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsPreformattedLineBreakFollowedByOtherBlockBoundary(
    const EditorDOMPointType& aPoint,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNewLine()) {
    return false;
  }
  if (aSkipWhiteSpaceStyleCheck == SkipWhiteSpaceStyleCheck::No &&
      !EditorUtils::IsNewLinePreformatted(
          *aPoint.template ContainerAs<Text>())) {
    return false;
  }
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we can treat it's not
          // followed by a block boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          aPoint.template NextPoint<EditorRawDOMPoint>(), aAncestorLimiter);
  if (!followingThing.ReachedOtherBlockElement()) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ElementPtr();
  }
  return true;
}

bool HTMLEditUtils::IsBRElementFollowedByLineBoundary(
    const dom::HTMLBRElement& aBRElement,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we cant treat it's not
          // followed by a block boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          EditorRawDOMPoint::After(aBRElement), aAncestorLimiter);
  if (!followingThing.ReachedLineBoundary()) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ReachedBlockBoundary()
                                          ? followingThing.ElementPtr()
                                          : nullptr;
  }
  return true;
}

bool HTMLEditUtils::IsBRElementFollowingCurrentBlockBoundary(
    const dom::HTMLBRElement& aBRElement,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aPrecedingBlockBoundaryElement /* = nullptr */) {
  if (aPrecedingBlockBoundaryElement) {
    *aPrecedingBlockBoundaryElement = nullptr;
  }
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, EditorRawDOMPoint(&aBRElement), aAncestorLimiter);
  if (!precedingThing.ReachedCurrentBlockBoundary()) {
    return false;
  }
  if (aPrecedingBlockBoundaryElement) {
    *aPrecedingBlockBoundaryElement = precedingThing.ElementPtr();
  }
  return true;
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsPreformattedLineBreakFollowingCurrentBlockBoundary(
    const EditorDOMPointType& aPoint,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aPrecedingBlockBoundaryElement /* = nullptr */) {
  if (aPrecedingBlockBoundaryElement) {
    *aPrecedingBlockBoundaryElement = nullptr;
  }
  if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNewLine()) {
    return false;
  }
  if (aSkipWhiteSpaceStyleCheck == SkipWhiteSpaceStyleCheck::No &&
      !EditorUtils::IsNewLinePreformatted(
          *aPoint.template ContainerAs<Text>())) {
    return false;
  }
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, aPoint, aAncestorLimiter);
  if (!precedingThing.ReachedCurrentBlockBoundary()) {
    return false;
  }
  if (aPrecedingBlockBoundaryElement) {
    *aPrecedingBlockBoundaryElement = precedingThing.ElementPtr();
  }
  return true;
}

bool HTMLEditUtils::IsBRElementFollowingLineBreak(
    const dom::HTMLBRElement& aBRElement,
    const Element* aAncestorLimiter /* = nullptr */) {
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, EditorRawDOMPoint(&aBRElement), aAncestorLimiter);
  return precedingThing.ReachedLineBreak();
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsPreformattedLineBreakFollowedByLineBoundary(
    const EditorDOMPointType& aPoint,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNewLine()) {
    return false;
  }
  if (aSkipWhiteSpaceStyleCheck == SkipWhiteSpaceStyleCheck::No &&
      !EditorUtils::IsNewLinePreformatted(
          *aPoint.template ContainerAs<Text>())) {
    return false;
  }
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we can treat it's not
          // followed by a line boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          aPoint.template NextPoint<EditorRawDOMPoint>(), aAncestorLimiter);
  if (!followingThing.ReachedLineBoundary()) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ReachedBlockBoundary()
                                          ? followingThing.ElementPtr()
                                          : nullptr;
  }
  return true;
}

bool HTMLEditUtils::IsBRElementFollowingLineBoundary(
    const dom::HTMLBRElement& aBRElement,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aPrecedingBlockBoundaryElement /* = nullptr */) {
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, EditorRawDOMPoint(&aBRElement), aAncestorLimiter);
  if (!precedingThing.ReachedLineBoundary()) {
    return false;
  }
  if (aPrecedingBlockBoundaryElement) {
    *aPrecedingBlockBoundaryElement = precedingThing.ReachedBlockBoundary()
                                          ? precedingThing.ElementPtr()
                                          : nullptr;
  }
  return true;
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBoundary(
    const EditorDOMPointType& aPoint,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aPrecedingBlockBoundaryElement /* = nullptr */) {
  if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNewLine()) {
    return false;
  }
  if (aSkipWhiteSpaceStyleCheck == SkipWhiteSpaceStyleCheck::No &&
      !EditorUtils::IsNewLinePreformatted(
          *aPoint.template ContainerAs<Text>())) {
    return false;
  }
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, aPoint, aAncestorLimiter);
  if (!precedingThing.ReachedLineBoundary()) {
    return false;
  }
  if (aPrecedingBlockBoundaryElement) {
    *aPrecedingBlockBoundaryElement = precedingThing.ReachedBlockBoundary()
                                          ? precedingThing.ElementPtr()
                                          : nullptr;
  }
  return true;
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsPreformattedLineBreakFollowingLineBreak(
    const EditorDOMPointType& aPoint,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */) {
  if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNewLine()) {
    return false;
  }
  if (aSkipWhiteSpaceStyleCheck == SkipWhiteSpaceStyleCheck::No &&
      !EditorUtils::IsNewLinePreformatted(
          *aPoint.template ContainerAs<Text>())) {
    return false;
  }
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, aPoint, aAncestorLimiter);
  return precedingThing.ReachedLineBreak();
}

bool HTMLEditUtils::IsUnnecessaryBRElement(
    const dom::HTMLBRElement& aBRElement,
    TreatAsUnnecessaryLineBreak aTreatAsUnnecessaryLineBreak,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  // If the <br> is followed by a block boundary and does not follow another
  // line break nor another block boundary, the <br> does not affect to the
  // layout so that it's unnecessary.
  Element* followingBlockBoundaryElement = nullptr;
  if (!HTMLEditUtils::IsBRElementFollowedByBlockBoundary(
          aBRElement, aAncestorLimiter, &followingBlockBoundaryElement)) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingBlockBoundaryElement;
  }
  Element* precedingBlockBoundaryElement = nullptr;
  if (HTMLEditUtils::IsBRElementFollowingLineBoundary(
          aBRElement, aAncestorLimiter, &precedingBlockBoundaryElement)) {
    if (followingBlockBoundaryElement == precedingBlockBoundaryElement) {
      // <br> in an empty block and the caller wants to treat it as unnecessary.
      return aTreatAsUnnecessaryLineBreak.contains(
          LineBreakFor::PaddingForEmptyBlock);
    }
    // <br> between a block boundary or a line break and a block boundary.
    // That's significant by default because of making an empty line before the
    // following block boundary.
    return aTreatAsUnnecessaryLineBreak.contains(
        LineBreakFor::EmptyLineBeforeBlockBoundary);
  }
  return true;
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsUnnecessaryPreformattedLineBreak(
    const EditorDOMPointType& aPoint,
    TreatAsUnnecessaryLineBreak aTreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  // If the linefeed is not followed by a block boundary, it causes a line
  // break.  So, let's check whether it's followed by a block boundary first.
  Element* followingBlockBoundaryElement = nullptr;
  if (!HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
          aPoint, aSkipWhiteSpaceStyleCheck, aAncestorLimiter,
          &followingBlockBoundaryElement)) {
    return false;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingBlockBoundaryElement;
  }
  MOZ_ASSERT(aPoint.IsInTextNode());
  MOZ_ASSERT(aPoint.IsCharNewLine());
  // The linefeed is followed by a block boundary, then, it's unnecessary
  // unless:
  // - it follows another line break
  // - it follows another block boundary
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, aPoint, aAncestorLimiter);
  if (precedingThing.ReachedCurrentBlockBoundary() &&
      followingBlockBoundaryElement == precedingThing.ElementPtr()) {
    // Preformatted linefeed in an empty block and the caller wants to treat it
    // as unnecessary.
    return aTreatAsUnnecessaryLineBreak.contains(
        LineBreakFor::PaddingForEmptyBlock);
  }
  if (precedingThing.ReachedLineBoundary()) {
    // Preformatted linefeed between a block boundary or a line break and a
    // block boundary. That's significant by default because of making an empty
    // line before the following block boundary.
    return aTreatAsUnnecessaryLineBreak.contains(
        LineBreakFor::EmptyLineBeforeBlockBoundary);
  }
  return true;
}

template <typename EditorDOMPointType>
bool HTMLEditUtils::IsSignificantPreformattedLineBreak(
    const EditorDOMPointType& aPoint,
    TreatAsUnnecessaryLineBreak aTreatAsUnnecessaryLineBreak,
    SkipWhiteSpaceStyleCheck
        aSkipWhiteSpaceStyleCheck /* = SkipWhiteSpaceStyleCheck::No */,
    const Element* aAncestorLimiter /* = nullptr */,
    Element** aFollowingBlockBoundaryElement /* = nullptr */) {
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = nullptr;
  }
  if (!aPoint.IsInTextNode() || aPoint.IsEndOfContainer() ||
      !aPoint.IsCharNewLine()) {
    return false;
  }
  if (aSkipWhiteSpaceStyleCheck == SkipWhiteSpaceStyleCheck::No &&
      !EditorUtils::IsNewLinePreformatted(
          *aPoint.template ContainerAs<Text>())) {
    return false;
  }
  // If the linefeed is not followed by a block boundary, it causes a line
  // break.
  const WSScanResult followingThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // aBRElement causes a line break so that we can treat it's not
          // followed by a block boundary in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          aPoint.template NextPoint<EditorRawDOMPoint>(), aAncestorLimiter);
  if (!followingThing.ReachedBlockBoundary()) {
    return true;
  }
  if (aFollowingBlockBoundaryElement) {
    *aFollowingBlockBoundaryElement = followingThing.ElementPtr();
  }
  MOZ_ASSERT(aPoint.IsInTextNode());
  MOZ_ASSERT(aPoint.IsCharNewLine());
  // The linefeed is followed by a block boundary, then, it affects to the
  // layout when:
  // - it follows another line break
  // - it follows another block boundary
  const WSScanResult precedingThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, aPoint, aAncestorLimiter);
  if (followingThing.ReachedCurrentBlockBoundary() &&
      precedingThing.ReachedCurrentBlockBoundary()) {
    // Preformatted linefeed in an empty block and the caller wants to treat it
    // as unnecessary.
    return !aTreatAsUnnecessaryLineBreak.contains(
        LineBreakFor::PaddingForEmptyBlock);
  }
  if (precedingThing.ReachedLineBoundary()) {
    // Preformatted linefeed between a block boundary or a line break and a
    // block boundary. That's significant by default because of making an empty
    // line before the following block boundary.
    return !aTreatAsUnnecessaryLineBreak.contains(
        LineBreakFor::EmptyLineBeforeBlockBoundary);
  }
  return false;
}

template <typename EditorDOMPointType>
WSScanResult
HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
    const EditorDOMPointType& aPoint,
    TreatAsUnnecessaryLineBreak aTreatAsUnnecessaryLineBreaks,
    const Element& aEditingHost, const Element* aAncestorLimiter) {
  MOZ_ASSERT(aPoint.IsSetAndValid());

  WSScanResult nextThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          {}, aPoint, aAncestorLimiter);
  if (!nextThing.ReachedLineBreak()) {
    return nextThing;
  }
  WSScanResult nextThingOfLineBreak =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          // If there is a visible thing including empty inline elements,
          // the found line break makes a new line for the visible inline
          // element so that we can treat it's not followed by a block boundary
          // in such case.
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers},
          nextThing.PointAfterReachedContent<EditorRawDOMPoint>(),
          aAncestorLimiter);
  // If the line break is not followed by a block boundary, it's significant so
  // that we should not skip it.
  if (!nextThingOfLineBreak.ReachedBlockBoundary()) {
    MOZ_ASSERT(
        nextThing.CreateEditorLineBreak<EditorRawLineBreak>().IsSignificant(
            aTreatAsUnnecessaryLineBreaks, aAncestorLimiter));
    return nextThing;
  }
  const WSScanResult previousThingOfLineBreak =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          // Even if there is a visible empty inline elements, we can ignore
          // them because they will be removed together when deleting something
          // across the element.
          {}, nextThing.PointAtReachedContent<EditorRawDOMPoint>(),
          aAncestorLimiter);
  if (previousThingOfLineBreak.ReachedLineBoundary()) {
    const bool lineBreakForPaddingForEmptyBlock =
        previousThingOfLineBreak.ReachedCurrentBlockBoundary() &&
        nextThingOfLineBreak.ReachedCurrentBlockBoundary();
    if (lineBreakForPaddingForEmptyBlock) {
      // If the line break is padding for the empty block, it's significant
      // unless the caller wants to treat it as unnecessary.
      if (!aTreatAsUnnecessaryLineBreaks.contains(
              LineBreakFor::PaddingForEmptyBlock)) {
        return nextThing;
      }
    }
    // If the line break is for making an empty line before a block boundary,
    // it's significant unless the caller wants to treat it as unnecessary.
    else if (!aTreatAsUnnecessaryLineBreaks.contains(
                 LineBreakFor::EmptyLineBeforeBlockBoundary)) {
      return nextThing;
    }
  }
  EditorLineBreak unnecessaryLineBreak =
      nextThing.CreateEditorLineBreak<EditorLineBreak>();
  MOZ_ASSERT(unnecessaryLineBreak.IsUnnecessary(aTreatAsUnnecessaryLineBreaks,
                                                aAncestorLimiter));
  return WSScanResult(std::move(nextThingOfLineBreak),
                      std::move(unnecessaryLineBreak), aEditingHost);
}

template <typename EditorLineBreakType, typename EditorDOMPointType>
Maybe<EditorLineBreakType> HTMLEditUtils::GetPrecedingUnnecessaryLineBreak(
    const EditorDOMPointType& aPoint,
    const Element* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT(aPoint.IsSetAndValid());
  MOZ_ASSERT(aPoint.IsInContentNode());

  const WSScanResult previousThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          {WSRunScanner::Option::StopAtVisibleEmptyInlineContainers}, aPoint,
          aAncestorLimiter);
  if (!previousThing.ReachedLineBreak()) {
    return Nothing{};
  }
  auto lineBreak = previousThing.CreateEditorLineBreak<EditorLineBreakType>();
  if (lineBreak.IsUnnecessary(TreatAsUnnecessaryLineBreak{})) {
    return Some(lineBreak);
  }
  return Nothing{};
}

template <typename PT, typename CT>
EditorDOMPoint HTMLEditUtils::LineRequiresPaddingLineBreakToBeVisible(
    const EditorDOMPointBase<PT, CT>& aPoint, const Element& aEditingHost) {
  if (MOZ_UNLIKELY(!aPoint.IsInContentNode())) {
    return EditorDOMPoint();
  }
  MOZ_ASSERT(HTMLEditUtils::NodeIsEditableOrNotInComposedDoc(
      *aPoint.template ContainerAs<nsIContent>()));
  // First, if the container is an element node, get the next deepest point.
  EditorRawDOMPoint point = aPoint.template To<EditorRawDOMPoint>();
  if (point.IsContainerElement()) {
    for (nsIContent* child = point.GetChild(); child;
         child = child->GetFirstChild()) {
      if (child->IsHTMLElement(nsGkAtoms::br)) {
        return EditorDOMPoint();
      }
      if (!HTMLEditUtils::NodeIsEditableOrNotInComposedDoc(*child) ||
          HTMLEditUtils::IsBlockElement(
              *child, BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
          (child->IsElement() && !HTMLEditUtils::IsContainerNode(*child))) {
        break;
      }
      point.Set(child, 0);
    }
  }
  // If the point is in a Text, check the next character in it to skip the
  // expensive check below.
  if (point.IsInTextNode() && !point.IsContainerEmpty()) {
    if (!point.IsStartOfContainer() &&
        !point.IsPreviousCharCollapsibleASCIISpace()) {
      return EditorDOMPoint();  // following a visible character.
    }
    if (!point.IsEndOfContainer()) {
      if (EditorUtils::IsWhiteSpacePreformatted(*point.ContainerAs<Text>())) {
        return EditorDOMPoint();  // followed by a visible character.
      }
      // NOTE: In the worst case, the fragment has a lot of collapsible
      // white-spaces after the point.  However, it won't occur with usual web
      // apps.  Instead, we should optimize the response time when user typing
      // keys in the usual web apps.
      const CharacterDataBuffer& characterDataBuffer =
          point.template ContainerAs<Text>()->DataBuffer();
      const uint32_t inclusiveNextVisibleCharOffset =
          characterDataBuffer.FindNonWhitespaceChar(
              EditorUtils::IsNewLinePreformatted(*point.ContainerAs<Text>())
                  ? WhitespaceOptions{WhitespaceOption::FormFeedIsSignificant,
                                      WhitespaceOption::NewLineIsSignificant}
                  : WhitespaceOptions{WhitespaceOption::FormFeedIsSignificant},
              point.Offset());
      if (inclusiveNextVisibleCharOffset != CharacterDataBuffer::kNotFound) {
        return EditorDOMPoint();  // followed by a visible character.
      }
      // Followed by only collapsible white-spaces, let's check the next visible
      // thing.
    }
  }

  const auto AdjustPointToInsertPaddingLineBreak =
      [](EditorDOMPoint& aPointToInsertLineBreak,
         const Element* aParentBlockElement, const Element& aEditingHost) {
        if (MOZ_UNLIKELY(!aPointToInsertLineBreak.IsInContentNode())) {
          aPointToInsertLineBreak.Clear();
          return;
        }
        while (MOZ_UNLIKELY(
            !HTMLEditUtils::CanNodeContain(
                *aPointToInsertLineBreak.GetContainer(), *nsGkAtoms::br) ||
            !HTMLEditUtils::NodeIsEditableOrNotInComposedDoc(
                *aPointToInsertLineBreak.GetContainer()))) {
          if (MOZ_UNLIKELY(aPointToInsertLineBreak.GetContainer() ==
                               aParentBlockElement ||
                           aPointToInsertLineBreak.GetContainer() ==
                               &aEditingHost)) {
            aPointToInsertLineBreak.Clear();
            return;
          }
          aPointToInsertLineBreak.SetAfterContainer();
          if (MOZ_UNLIKELY(!aPointToInsertLineBreak.IsInContentNode())) {
            aPointToInsertLineBreak.Clear();
            return;
          }
        }
      };

  // If the point is in an empty block, we can skip the expensive check below
  // too.
  const Element* maybeNonEditableBlock =
      HTMLEditUtils::GetInclusiveAncestorElement(
          *point.ContainerAs<nsIContent>(), ClosestBlockElement,
          BlockInlineCheck::UseComputedDisplayStyle);
  if (maybeNonEditableBlock &&
      HTMLEditUtils::IsEmptyNode(
          *maybeNonEditableBlock,
          {EmptyCheckOption::TreatSingleBRElementAsVisible})) {
    EditorDOMPoint pointToInsertLineBreak =
        HTMLEditUtils::GetDeepestEditableEndPointOf<EditorDOMPoint>(
            *maybeNonEditableBlock,
            {EditablePointOption::RecognizeInvisibleWhiteSpaces,
             EditablePointOption::StopAtComment});
    if (pointToInsertLineBreak.IsInTextNode()) {
      pointToInsertLineBreak.SetAfterContainer();
    }
    AdjustPointToInsertPaddingLineBreak(pointToInsertLineBreak,
                                        maybeNonEditableBlock, aEditingHost);
    return pointToInsertLineBreak;
  }

  EditorDOMPoint preferredPaddingLineBreakPoint;
  const bool followedByBlockBoundary = [&]() {
    if (point.GetContainer() == maybeNonEditableBlock &&
        point.IsEndOfContainer()) {
      preferredPaddingLineBreakPoint = point.To<EditorDOMPoint>();
      return true;
    }
    if (point.GetContainer() == &aEditingHost && point.IsEndOfContainer()) {
      return false;
    }
    const WSScanResult nextThing =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary({}, point);
    if (nextThing.ReachedBlockBoundary()) {
      if (nextThing.ReachedCurrentBlockBoundary()) {
        preferredPaddingLineBreakPoint = point.AfterContainer<EditorDOMPoint>();
      } else {
        preferredPaddingLineBreakPoint = point.To<EditorDOMPoint>();
      }
      // FIXME: Scan an editable point to put a padding <br>.
      if (NS_WARN_IF(!HTMLEditUtils::NodeIsEditableOrNotInComposedDoc(
              *preferredPaddingLineBreakPoint.GetContainer()))) {
        return false;
      }
      return true;
    }
    return false;
  }();
  if (!followedByBlockBoundary) {
    return EditorDOMPoint();
  }
  const bool isFollowingBlockBoundary = [&]() {
    if (point.GetContainer() == maybeNonEditableBlock &&
        point.IsStartOfContainer()) {
      return true;
    }
    // We need to scan previous `Text` which may ends with invisible white-space
    // because we want to make it visible.  Therefore, we cannot use
    // WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary() here.
    nsIContent* const previousVisibleLeafOrChildBlock =
        HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElement(
            preferredPaddingLineBreakPoint,
            {LeafNodeOption::TreatChildBlockAsLeafNode,
             LeafNodeOption::IgnoreInvisibleEmptyInlineContainers,
             LeafNodeOption::IgnoreEmptyText},
            BlockInlineCheck::Auto);
    if (!previousVisibleLeafOrChildBlock) {
      // Reached current block.
      return true;
    }
    return HTMLEditUtils::IsBlockElement(
        *previousVisibleLeafOrChildBlock,
        BlockInlineCheck::UseComputedDisplayOutsideStyle);
  }();
  if (!isFollowingBlockBoundary) {
    return EditorDOMPoint();
  }
  AdjustPointToInsertPaddingLineBreak(preferredPaddingLineBreakPoint,
                                      maybeNonEditableBlock, aEditingHost);
  return preferredPaddingLineBreakPoint;
}

template <typename PT, typename CT>
bool HTMLEditUtils::PointIsImmediatelyBeforeCurrentBlockBoundary(
    const EditorDOMPointBase<PT, CT>& aPoint,
    IgnoreInvisibleLineBreak aIgnoreInvisibleLineBreak) {
  MOZ_ASSERT(aPoint.IsSetAndValidInComposedDoc());

  if (MOZ_UNLIKELY(!aPoint.IsInContentNode())) {
    return false;
  }
  const WSScanResult nextThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          {WSRunScanner::Option::OnlyEditableNodes}, aPoint);
  if (nextThing.ReachedCurrentBlockBoundary()) {
    return true;
  }
  if (aIgnoreInvisibleLineBreak == IgnoreInvisibleLineBreak::No ||
      !nextThing.ReachedLineBreak()) {
    return false;
  }
  const EditorRawLineBreak lineBreak =
      nextThing.CreateEditorLineBreak<EditorRawLineBreak>();
  return lineBreak.IsFollowedByCurrentBlockBoundary();
}

// static
HTMLEditUtils::LeafNodeType HTMLEditUtils::GetLeafNodeType(
    const nsIContent& aContent, const LeafNodeOptions& aOptions,
    BlockInlineCheck aBlockInlineCheck, IgnoreChildren aIgnoreChildren) {
  if (!HTMLEditUtils::IsSimplyEditableNode(aContent)) {
    if (aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode)) {
      return LeafNodeType::Leaf;
    }
    if (aOptions.contains(LeafNodeOption::IgnoreNonEditableNode)) {
      return LeafNodeType::Ignore;
    }
  }
  if (const Element* const element = Element::FromNode(&aContent)) {
    // If the element is a replaced element, it should be treated as a leaf.
    if (HTMLEditUtils::IsReplacedElement(*element)) {
      return LeafNodeType::Leaf;
    }
    // If the element has a shadow root, its children will be replaced with the
    // shadow root children which are not editable.  Therefore, we should treat
    // it as a leaf.
    // XXX Should we check whether it's empty and/or invisible? In theory, it
    // should be yes. However, web apps shouldn't create empty shadow DOM so
    // that it must be okay for now.
    if (element->GetShadowRootForSelection()) {
      return LeafNodeType::Leaf;
    }
    // We're looking for a child block, check the display-outside style.
    if (aOptions.contains(LeafNodeOption::TreatChildBlockAsLeafNode) &&
        HTMLEditUtils::IsBlockElement(
            *element,
            UseComputedDisplayOutsideStyleIfAuto(aBlockInlineCheck))) {
      return LeafNodeType::Leaf;
    }
    // Let's handle invisible void elements even if it has some children.
    if (!HTMLEditUtils::IsContainerNode(*element)) {
      return aOptions.contains(
                 LeafNodeOption::IgnoreInvisibleInlineVoidElements) &&
                     (!HTMLEditUtils::IsVisibleElementEvenIfLeafNode(
                          *element) ||
                      HTMLEditUtils::IsInclusiveAncestorCSSDisplayNone(
                          *element))
                 ? LeafNodeType::Ignore
                 : LeafNodeType::Leaf;
    }
    if (aIgnoreChildren == IgnoreChildren::No && aContent.HasChildNodes()) {
      return LeafNodeType::NonEmptyContainer;
    }
    // if the element is a flow root, it's meaningful and must be visible.
    if (HTMLEditUtils::IsBlockElement(
            *element, aBlockInlineCheck == BlockInlineCheck::UseHTMLDefaultStyle
                          ? BlockInlineCheck::UseHTMLDefaultStyle
                          : BlockInlineCheck::UseComputedDisplayStyle)) {
      return LeafNodeType::Leaf;
    }
    // Now the element is an empty inline container like <span></span>.
    if (aOptions.contains(LeafNodeOption::IgnoreAnyEmptyInlineContainers)) {
      return LeafNodeType::Ignore;
    }
    if (aOptions.contains(
            LeafNodeOption::IgnoreInvisibleEmptyInlineContainers) &&
        (!HTMLEditUtils::IsVisibleElementEvenIfLeafNode(*element) ||
         HTMLEditUtils::IsInclusiveAncestorCSSDisplayNone(*element))) {
      return LeafNodeType::Ignore;
    }
    return LeafNodeType::Leaf;
  }
  if (const Text* const text = Text::FromNode(aContent)) {
    if (!text->TextDataLength()) {
      return aOptions.contains(LeafNodeOption::IgnoreEmptyText) ||
                     aOptions.contains(LeafNodeOption::IgnoreInvisibleText)
                 ? LeafNodeType::Ignore
                 : LeafNodeType::Leaf;
    }
    return !aOptions.contains(LeafNodeOption::IgnoreInvisibleText) ||
                   (HTMLEditUtils::IsVisibleTextNode(
                        *text, TreatInvisibleLineBreakAs::Visible) &&
                    !HTMLEditUtils::IsInclusiveAncestorCSSDisplayNone(*text))
               ? LeafNodeType::Leaf
               : LeafNodeType::Ignore;
  }
  if (aContent.IsComment()) {
    return aOptions.contains(LeafNodeOption::TreatCommentAsLeafNode)
               ? LeafNodeType::Leaf
               : LeafNodeType::Ignore;
  }
  return LeafNodeType::Ignore;
}

// static
nsIContent* HTMLEditUtils::GetLastLeafContent(
    const nsINode& aNode, const LeafNodeOptions& aOptions,
    BlockInlineCheck aBlockInlineCheck /* = BlockInlineCheck::Unused */) {
  MOZ_ASSERT_IF(
      aOptions.contains(LeafNodeOption::IgnoreNonEditableNode),
      !aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode));
  MOZ_ASSERT_IF(aOptions.contains(LeafNodeOption::TreatChildBlockAsLeafNode),
                aBlockInlineCheck != BlockInlineCheck::Unused);
  // editor shouldn't touch child nodes which are replaced with native
  // anonymous nodes.
  if (aNode.IsElement() &&
      HTMLEditUtils::IsNeverElementContentsEditableByUser(*aNode.AsElement())) {
    return nullptr;
  }
  for (nsIContent* content = aNode.GetLastChild(); content;) {
    const LeafNodeType type = HTMLEditUtils::GetLeafNodeType(
        *content, aOptions, aBlockInlineCheck, IgnoreChildren::No);
    if (type == LeafNodeType::Leaf) {
      return content;
    }
    if (type == LeafNodeType::NonEmptyContainer) {
      content = content->GetLastChild();
      MOZ_ASSERT(content);
      continue;
    }
    MOZ_ASSERT(type == LeafNodeType::Ignore);
    nsIContent* const prevSibling = content->GetPreviousSibling();
    if (prevSibling) {
      content = prevSibling;
      continue;
    }
    // Okay, content is the first sibling but no meaningful content is not in
    // current container.  So, the container can be treated as an empty
    // container.
    nsIContent* const parent = content->GetParent();
    if (!parent || parent == &aNode) {
      return nullptr;
    }
    content = nullptr;
    for (nsIContent* const ancestor :
         parent->InclusiveAncestorsOfType<nsIContent>()) {
      if (ancestor == &aNode) {
        return nullptr;  // No meaningful leaf in aNode.
      }
      // All children of current content is ignorable.  So, the parent
      // should be treated as empty.
      const LeafNodeType type = HTMLEditUtils::GetLeafNodeType(
          *ancestor, aOptions, aBlockInlineCheck, IgnoreChildren::Yes);
      if (type == LeafNodeType::Leaf) {
        return ancestor;
      }
      MOZ_ASSERT(type == LeafNodeType::Ignore);
      // If the ancestor has a previous sibling, check it.
      if ((content = ancestor->GetPreviousSibling())) {
        break;
      }
      // Otherwise, check the parent of the ancestor.
    }
  }
  return nullptr;
}

// static
nsIContent* HTMLEditUtils::GetFirstLeafContent(
    const nsINode& aNode, const LeafNodeOptions& aOptions,
    BlockInlineCheck aBlockInlineCheck /* = BlockInlineCheck::Unused */) {
  MOZ_ASSERT_IF(
      aOptions.contains(LeafNodeOption::IgnoreNonEditableNode),
      !aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode));
  MOZ_ASSERT_IF(aOptions.contains(LeafNodeOption::TreatChildBlockAsLeafNode),
                aBlockInlineCheck != BlockInlineCheck::Unused);
  // editor shouldn't touch child nodes which are replaced with native
  // anonymous nodes.
  if (aNode.IsElement() &&
      HTMLEditUtils::IsNeverElementContentsEditableByUser(*aNode.AsElement())) {
    return nullptr;
  }
  for (nsIContent* content = aNode.GetFirstChild(); content;) {
    const LeafNodeType type = HTMLEditUtils::GetLeafNodeType(
        *content, aOptions, aBlockInlineCheck, IgnoreChildren::No);
    if (type == LeafNodeType::Leaf) {
      return content;
    }
    if (type == LeafNodeType::NonEmptyContainer) {
      content = content->GetFirstChild();
      MOZ_ASSERT(content);
      continue;
    }
    MOZ_ASSERT(type == LeafNodeType::Ignore);
    nsIContent* const nextSibling = content->GetNextSibling();
    if (nextSibling) {
      content = nextSibling;
      continue;
    }
    // Okay, content is the last sibling but no meaningful content is not in
    // current container.  So, the container can be treated as an empty
    // container.
    nsIContent* const parent = content->GetParent();
    if (!parent || parent == &aNode) {
      return nullptr;  // No meaningful leaf in aNode.
    }
    content = nullptr;
    for (nsIContent* const ancestor :
         parent->InclusiveAncestorsOfType<nsIContent>()) {
      if (ancestor == &aNode) {
        return nullptr;
      }
      // All children of current content is ignorable.  So, the parent
      // should be treated as empty.
      const LeafNodeType type = HTMLEditUtils::GetLeafNodeType(
          *ancestor, aOptions, aBlockInlineCheck, IgnoreChildren::Yes);
      if (type == LeafNodeType::Leaf) {
        return ancestor;
      }
      MOZ_ASSERT(type == LeafNodeType::Ignore);
      // If the ancestor has a next sibling, check it.
      if ((content = ancestor->GetNextSibling())) {
        break;
      }
      // Otherwise, check the parent of the ancestor.
    }
  }
  return nullptr;
}

// static
nsIContent* HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
    const nsIContent& aStartContent, StopAtBlockSibling aStopAtBlockSibling,
    const LeafNodeOptions& aOptions, BlockInlineCheck aBlockInlineCheck,
    const Element* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT_IF(
      aOptions.contains(LeafNodeOption::IgnoreNonEditableNode),
      !aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode));

  if (&aStartContent == aAncestorLimiter) {
    return nullptr;
  }

  Element* container = aStartContent.GetParentElement();
  for (nsIContent* nextContent = aStartContent.GetNextSibling();;) {
    if (!nextContent) {
      if (!container) {
        NS_WARNING("Reached orphan node while climbing up the DOM tree");
        return nullptr;
      }
      for (Element* const parentElement :
           container->InclusiveAncestorsOfType<Element>()) {
        if (parentElement == aAncestorLimiter ||
            (static_cast<bool>(aStopAtBlockSibling) &&
             HTMLEditUtils::IsBlockElement(
                 *parentElement,
                 UseComputedDisplayStyleIfAuto(aBlockInlineCheck)))) {
          return nullptr;
        }
        if (aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode) &&
            !parentElement->IsEditable()) {
          return nullptr;
        }
        nextContent = parentElement->GetNextSibling();
        if (nextContent) {
          container = nextContent->GetParentElement();
          break;
        }
        if (!parentElement->GetParentElement()) {
          NS_WARNING("Reached orphan node while climbing up the DOM tree");
          return nullptr;
        }
      }
      MOZ_ASSERT(nextContent);
    }

    // We have a next content.  If it's a block, return it.
    if (static_cast<bool>(aStopAtBlockSibling) &&
        HTMLEditUtils::IsBlockElement(
            *nextContent,
            PreferDisplayOutsideIfUsingDisplay(
                UseComputedDisplayStyleIfAuto(aBlockInlineCheck)))) {
      return nextContent;
    }
    LeafNodeType type = HTMLEditUtils::GetLeafNodeType(
        *nextContent, aOptions, aBlockInlineCheck, IgnoreChildren::No);
    if (type == LeafNodeType::Leaf) {
      return nextContent;
    }
    if (type == LeafNodeType::Ignore) {
      nextContent = nextContent->GetNextSibling();
      MOZ_ASSERT_IF(nextContent, container == nextContent->GetParentElement());
      continue;
    }
    MOZ_ASSERT(type == LeafNodeType::NonEmptyContainer);
    if (nsIContent* const lastLeaf = HTMLEditUtils::GetFirstLeafContent(
            *nextContent, aOptions,
            PreferDisplayOutsideIfUsingDisplay(aBlockInlineCheck))) {
      return lastLeaf;
    }
    // nextContent has some nodes, but does not have meaningful nodes.
    // Therefore, we can treat it as empty.
    type = HTMLEditUtils::GetLeafNodeType(
        *nextContent, aOptions, aBlockInlineCheck, IgnoreChildren::Yes);
    if (type == LeafNodeType::Leaf) {
      return nextContent;
    }
    MOZ_ASSERT(type == LeafNodeType::Ignore);
    nextContent = nextContent->GetNextSibling();
    MOZ_ASSERT_IF(nextContent, container == nextContent->GetParentElement());
  }
  MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE(
      "Must return from the preceding for-loop");
}

// static
template <typename PT, typename CT>
nsIContent* HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
    const EditorDOMPointBase<PT, CT>& aStartPoint,
    StopAtBlockSibling aStopAtBlockSibling, const LeafNodeOptions& aOptions,
    BlockInlineCheck aBlockInlineCheck,
    const Element* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT(aStartPoint.IsSet());
  MOZ_ASSERT_IF(
      aOptions.contains(LeafNodeOption::IgnoreNonEditableNode),
      !aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode));

  if (!aStartPoint.IsInContentNode()) {
    return nullptr;
  }
  if (!aStartPoint.GetContainer()->IsElement()) {
    return HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
        *aStartPoint.template ContainerAs<nsIContent>(), aStopAtBlockSibling,
        aOptions, aBlockInlineCheck, aAncestorLimiter);
  }
  if (!HTMLEditUtils::IsContainerNode(
          *aStartPoint.template ContainerAs<Element>()) ||
      HTMLEditUtils::IsReplacedElement(
          *aStartPoint.template ContainerAs<Element>())) {
    return HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
        *aStartPoint.template ContainerAs<nsIContent>(), aStopAtBlockSibling,
        aOptions, aBlockInlineCheck, aAncestorLimiter);
  }

  for (nsIContent* nextContent = aStartPoint.GetChild();;) {
    if (!nextContent) {
      if (aStartPoint.GetContainer() == aAncestorLimiter ||
          (static_cast<bool>(aStopAtBlockSibling) &&
           HTMLEditUtils::IsBlockElement(
               *aStartPoint.template ContainerAs<Element>(),
               UseComputedDisplayStyleIfAuto(aBlockInlineCheck)))) {
        // We are at end of the block.
        return nullptr;
      }

      // We are at end of non-block container
      return HTMLEditUtils::GetNextLeafContentOrNextBlockElementImpl(
          *aStartPoint.template ContainerAs<Element>(), aStopAtBlockSibling,
          aOptions, PreferDisplayOutsideIfUsingDisplay(aBlockInlineCheck),
          aAncestorLimiter);
    }

    // We have a next node.  If it's a block, return it.
    if (static_cast<bool>(aStopAtBlockSibling) &&
        HTMLEditUtils::IsBlockElement(
            *nextContent,
            UseComputedDisplayOutsideStyleIfAuto(aBlockInlineCheck))) {
      return nextContent;
    }
    LeafNodeType type = HTMLEditUtils::GetLeafNodeType(
        *nextContent, aOptions, aBlockInlineCheck, IgnoreChildren::No);
    if (type == LeafNodeType::Leaf) {
      return nextContent;
    }
    if (type == LeafNodeType::Ignore) {
      nextContent = nextContent->GetNextSibling();
      continue;
    }
    MOZ_ASSERT(type == LeafNodeType::NonEmptyContainer);
    if (nsIContent* const firstLeaf = HTMLEditUtils::GetFirstLeafContent(
            *nextContent, aOptions,
            PreferDisplayOutsideIfUsingDisplay(aBlockInlineCheck))) {
      return firstLeaf;
    }
    // nextContent has some nodes, but does not have meaningful nodes.
    // Therefore, we can treat it as empty.
    type = HTMLEditUtils::GetLeafNodeType(
        *nextContent, aOptions, aBlockInlineCheck, IgnoreChildren::Yes);
    if (type == LeafNodeType::Leaf) {
      return nextContent;
    }
    MOZ_ASSERT(type == LeafNodeType::Ignore);
    nextContent = nextContent->GetNextSibling();
  }
  MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE(
      "Must return from the preceding for-loop");
}

// static
nsIContent* HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
    const nsIContent& aStartContent, StopAtBlockSibling aStopAtBlockSibling,
    const LeafNodeOptions& aOptions, BlockInlineCheck aBlockInlineCheck,
    const Element* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT_IF(
      aOptions.contains(LeafNodeOption::IgnoreNonEditableNode),
      !aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode));

  if (&aStartContent == aAncestorLimiter) {
    return nullptr;
  }

  Element* container = aStartContent.GetParentElement();
  for (nsIContent* previousContent = aStartContent.GetPreviousSibling();;) {
    if (!previousContent) {
      if (!container) {
        NS_WARNING("Reached orphan node while climbing up the DOM tree");
        return nullptr;
      }
      for (Element* parentElement :
           container->InclusiveAncestorsOfType<Element>()) {
        if (parentElement == aAncestorLimiter ||
            (static_cast<bool>(aStopAtBlockSibling) &&
             HTMLEditUtils::IsBlockElement(
                 *parentElement,
                 UseComputedDisplayStyleIfAuto(aBlockInlineCheck)))) {
          return nullptr;
        }
        if (aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode) &&
            !parentElement->IsEditable()) {
          return nullptr;
        }
        previousContent = parentElement->GetPreviousSibling();
        if (previousContent) {
          container = previousContent->GetParentElement();
          break;
        }
        if (!parentElement->GetParentElement()) {
          NS_WARNING("Reached orphan node while climbing up the DOM tree");
          return nullptr;
        }
      }
      MOZ_ASSERT(previousContent);
    }
    // We have a next content.  If it's a block, return it.
    if (static_cast<bool>(aStopAtBlockSibling) &&
        HTMLEditUtils::IsBlockElement(
            *previousContent,
            PreferDisplayOutsideIfUsingDisplay(
                UseComputedDisplayOutsideStyleIfAuto(aBlockInlineCheck)))) {
      return previousContent;
    }
    LeafNodeType type = HTMLEditUtils::GetLeafNodeType(
        *previousContent, aOptions, aBlockInlineCheck, IgnoreChildren::No);
    if (type == LeafNodeType::Leaf) {
      return previousContent;
    }
    if (type == LeafNodeType::Ignore) {
      previousContent = previousContent->GetPreviousSibling();
      MOZ_ASSERT_IF(previousContent,
                    container == previousContent->GetParentElement());
      continue;
    }
    if (nsIContent* const lastLeaf = HTMLEditUtils::GetLastLeafContent(
            *previousContent, aOptions,
            PreferDisplayOutsideIfUsingDisplay(aBlockInlineCheck))) {
      return lastLeaf;
    }
    // previousContent has some nodes, but does not have meaningful nodes.
    // Therefore, we can treat it as empty.
    type = HTMLEditUtils::GetLeafNodeType(
        *previousContent, aOptions, aBlockInlineCheck, IgnoreChildren::Yes);
    if (type == LeafNodeType::Leaf) {
      return previousContent;
    }
    MOZ_ASSERT(type == LeafNodeType::Ignore);
    previousContent = previousContent->GetPreviousSibling();
    MOZ_ASSERT_IF(previousContent,
                  container == previousContent->GetParentElement());
    return previousContent;
  }
  MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE(
      "Must return from the preceding for-loop");
}

// static
template <typename PT, typename CT>
nsIContent* HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
    const EditorDOMPointBase<PT, CT>& aStartPoint,
    StopAtBlockSibling aStopAtBlockSibling, const LeafNodeOptions& aOptions,
    BlockInlineCheck aBlockInlineCheck,
    const Element* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT(aStartPoint.IsSet());
  MOZ_ASSERT_IF(
      aOptions.contains(LeafNodeOption::IgnoreNonEditableNode),
      !aOptions.contains(LeafNodeOption::TreatNonEditableNodeAsLeafNode));

  if (!aStartPoint.IsInContentNode()) {
    return nullptr;
  }
  if (!aStartPoint.GetContainer()->IsElement()) {
    return HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
        *aStartPoint.template ContainerAs<nsIContent>(), aStopAtBlockSibling,
        aOptions, aBlockInlineCheck, aAncestorLimiter);
  }
  if (!HTMLEditUtils::IsContainerNode(
          *aStartPoint.template ContainerAs<Element>()) ||
      HTMLEditUtils::IsReplacedElement(
          *aStartPoint.template ContainerAs<Element>())) {
    return HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
        *aStartPoint.template ContainerAs<Element>(), aStopAtBlockSibling,
        aOptions, aBlockInlineCheck, aAncestorLimiter);
  }

  if (aStartPoint.IsStartOfContainer()) {
    if (aStartPoint.GetContainer() == aAncestorLimiter ||
        (static_cast<bool>(aStopAtBlockSibling) &&
         HTMLEditUtils::IsBlockElement(
             *aStartPoint.template ContainerAs<Element>(),
             UseComputedDisplayStyleIfAuto(aBlockInlineCheck)))) {
      // We are at start of the block.
      return nullptr;
    }

    // We are at start of non-block container
    return HTMLEditUtils::GetPreviousLeafContentOrPreviousBlockElementImpl(
        *aStartPoint.template ContainerAs<Element>(), aStopAtBlockSibling,
        aOptions, PreferDisplayOutsideIfUsingDisplay(aBlockInlineCheck),
        aAncestorLimiter);
  }

  for (nsIContent* previousContent = aStartPoint.GetPreviousSiblingOfChild();
       previousContent;
       previousContent = previousContent->GetPreviousSibling()) {
    // We have a prior node.  If it's a block, return it.
    if (static_cast<bool>(aStopAtBlockSibling) &&
        HTMLEditUtils::IsBlockElement(
            *previousContent,
            UseComputedDisplayOutsideStyleIfAuto(aBlockInlineCheck))) {
      return previousContent;
    }
    LeafNodeType type = HTMLEditUtils::GetLeafNodeType(
        *previousContent, aOptions, aBlockInlineCheck, IgnoreChildren::No);
    if (type == LeafNodeType::Leaf) {
      return previousContent;
    }
    if (type == LeafNodeType::Ignore) {
      continue;
    }
    if (nsIContent* const lastLeaf = HTMLEditUtils::GetLastLeafContent(
            *previousContent, aOptions,
            PreferDisplayOutsideIfUsingDisplay(aBlockInlineCheck))) {
      return lastLeaf;
    }
    // previousContent has some nodes, but does not have meaningful nodes.
    // Therefore, we can treat it as empty.
    type = HTMLEditUtils::GetLeafNodeType(
        *previousContent, aOptions, aBlockInlineCheck, IgnoreChildren::Yes);
    if (type == LeafNodeType::Leaf) {
      return previousContent;
    }
    MOZ_ASSERT(type == LeafNodeType::Ignore);
  }
  return nullptr;
}

nsIContent* HTMLEditUtils::GetSibling(const nsIContent& aContent,
                                      WalkTreeDirection aDirection,
                                      const LeafNodeOptions& aOptions,
                                      BlockInlineCheck aBlockInlineCheck) {
  MOZ_ASSERT(aBlockInlineCheck != BlockInlineCheck::Unused);
  aBlockInlineCheck = UseComputedDisplayOutsideStyleIfAuto(aBlockInlineCheck);
  for (nsIContent* sibling = aDirection == WalkTreeDirection::Backward
                                 ? aContent.GetPreviousSibling()
                                 : aContent.GetNextSibling();
       sibling; sibling = aDirection == WalkTreeDirection::Backward
                              ? sibling->GetPreviousSibling()
                              : sibling->GetNextSibling()) {
    const LeafNodeType leafNodeType = HTMLEditUtils::GetLeafNodeType(
        *sibling, aOptions, aBlockInlineCheck, IgnoreChildren::No);
    if (leafNodeType == LeafNodeType::Ignore) {
      continue;
    }
    if (HTMLEditUtils::IsBlockElement(*sibling, aBlockInlineCheck)) {
      return sibling;
    }
    if (leafNodeType == LeafNodeType::NonEmptyContainer) {
      if (HTMLEditUtils::GetFirstLeafContent(*sibling, aOptions,
                                             aBlockInlineCheck)) {
        return sibling;  // Has meaningful child so that it's meaningful.
      }
      if (HTMLEditUtils::GetLeafNodeType(*sibling, aOptions, aBlockInlineCheck,
                                         IgnoreChildren::Yes) ==
          LeafNodeType::Ignore) {
        continue;  // The sibling itself can be ignored.
      }
    }
    return sibling;
  }
  return nullptr;
}

nsIContent* HTMLEditUtils::GetFirstOrLastChild(
    const nsINode& aNode, FirstOrLastChild aFirstOrLastChild,
    const LeafNodeOptions& aOptions, BlockInlineCheck aBlockInlineCheck) {
  MOZ_ASSERT(aBlockInlineCheck != BlockInlineCheck::Unused);
  aBlockInlineCheck = UseComputedDisplayOutsideStyleIfAuto(aBlockInlineCheck);
  for (nsIContent* child = aFirstOrLastChild == FirstOrLastChild::First
                               ? aNode.GetFirstChild()
                               : aNode.GetLastChild();
       child; child = aFirstOrLastChild == FirstOrLastChild::First
                          ? child->GetNextSibling()
                          : child->GetPreviousSibling()) {
    const LeafNodeType leafNodeType = HTMLEditUtils::GetLeafNodeType(
        *child, aOptions, aBlockInlineCheck, IgnoreChildren::No);
    if (leafNodeType == LeafNodeType::Ignore) {
      continue;
    }
    if (HTMLEditUtils::IsBlockElement(*child, aBlockInlineCheck)) {
      return child;
    }
    if (leafNodeType == LeafNodeType::NonEmptyContainer) {
      if (HTMLEditUtils::GetFirstLeafContent(*child, aOptions,
                                             aBlockInlineCheck)) {
        return child;  // Has meaningful child so that it's meaningful.
      }
      if (HTMLEditUtils::GetLeafNodeType(*child, aOptions, aBlockInlineCheck,
                                         IgnoreChildren::Yes) ==
          LeafNodeType::Ignore) {
        continue;  // The child itself can be ignored.
      }
    }
    return child;
  }
  return nullptr;
}

uint32_t HTMLEditUtils::GetFirstVisibleCharOffset(const Text& aText) {
  const CharacterDataBuffer& characterDataBuffer = aText.DataBuffer();
  if (!characterDataBuffer.GetLength() ||
      !EditorRawDOMPointInText(&aText, 0u)
           .IsCharCollapsibleASCIISpaceOrNBSP()) {
    return 0u;
  }
  const WSScanResult previousThingOfText =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
          {}, EditorRawDOMPoint(&aText));
  if (!previousThingOfText.ReachedLineBoundary()) {
    return 0u;
  }
  return HTMLEditUtils::GetInclusiveNextNonCollapsibleCharOffset(aText, 0u)
      .valueOr(characterDataBuffer.GetLength());
}

uint32_t HTMLEditUtils::GetOffsetAfterLastVisibleChar(const Text& aText) {
  const CharacterDataBuffer& characterDataBuffer = aText.DataBuffer();
  if (!characterDataBuffer.GetLength()) {
    return 0u;
  }
  if (!EditorRawDOMPointInText::AtLastContentOf(aText)
           .IsCharCollapsibleASCIISpaceOrNBSP()) {
    return characterDataBuffer.GetLength();
  }
  const WSScanResult nextThingOfText =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
          {}, EditorRawDOMPoint::After(aText));
  if (!nextThingOfText.ReachedLineBoundary()) {
    return characterDataBuffer.GetLength();
  }
  const Maybe<uint32_t> lastNonCollapsibleCharOffset =
      HTMLEditUtils::GetPreviousNonCollapsibleCharOffset(
          aText, characterDataBuffer.GetLength());
  if (lastNonCollapsibleCharOffset.isNothing()) {
    return 0u;
  }
  if (*lastNonCollapsibleCharOffset == characterDataBuffer.GetLength() - 1u) {
    return characterDataBuffer.GetLength();
  }
  const uint32_t firstTrailingWhiteSpaceOffset =
      *lastNonCollapsibleCharOffset + 1u;
  MOZ_ASSERT(firstTrailingWhiteSpaceOffset < characterDataBuffer.GetLength());
  if (nextThingOfText.ReachedBlockBoundary()) {
    return firstTrailingWhiteSpaceOffset;
  }
  // If followed by <br> or preformatted line break, one white-space is
  // rendered.
  return firstTrailingWhiteSpaceOffset + 1u;
}

uint32_t HTMLEditUtils::GetInvisibleWhiteSpaceCount(
    const Text& aText, uint32_t aOffset /* = 0u */,
    uint32_t aLength /* = UINT32_MAX */) {
  const CharacterDataBuffer& characterDataBuffer = aText.DataBuffer();
  if (!aLength || characterDataBuffer.GetLength() <= aOffset) {
    return 0u;
  }
  const uint32_t endOffset = static_cast<uint32_t>(
      std::min(static_cast<uint64_t>(aOffset) + aLength,
               static_cast<uint64_t>(characterDataBuffer.GetLength())));
  const auto firstVisibleOffset = [&]() -> uint32_t {
    // If the white-space sequence follows a preformatted linebreak, ASCII
    // spaces at start are invisible.
    if (aOffset &&
        characterDataBuffer.CharAt(aOffset - 1u) == HTMLEditUtils::kNewLine &&
        EditorUtils::IsNewLinePreformatted(aText)) {
      for (const uint32_t offset : IntegerRange(aOffset, endOffset)) {
        if (characterDataBuffer.CharAt(offset) == HTMLEditUtils::kNBSP) {
          return offset;
        }
      }
      return endOffset;  // all white-spaces are invisible.
    }
    if (aOffset) {
      return aOffset - 1u;
    }
    return HTMLEditUtils::GetFirstVisibleCharOffset(aText);
  }();
  if (firstVisibleOffset >= endOffset) {
    return endOffset - aOffset;  // All white-spaces are invisible.
  }
  const auto afterLastVisibleOffset = [&]() -> uint32_t {
    // If the white-spaces are followed by a preformatted line break, ASCII
    // spaces at end are invisible.
    if (endOffset < characterDataBuffer.GetLength() &&
        characterDataBuffer.CharAt(endOffset) == HTMLEditUtils::kNewLine &&
        EditorUtils::IsNewLinePreformatted(aText)) {
      for (const uint32_t offset : Reversed(IntegerRange(aOffset, endOffset))) {
        if (characterDataBuffer.CharAt(offset) == HTMLEditUtils::kNBSP) {
          return offset + 1u;
        }
      }
      return aOffset;  // all white-spaces are invisible.
    }
    if (endOffset < characterDataBuffer.GetLength() - 1u) {
      return endOffset;
    }
    return HTMLEditUtils::GetOffsetAfterLastVisibleChar(aText);
  }();
  if (aOffset >= afterLastVisibleOffset) {
    return endOffset - aOffset;  // All white-spaces are invisible.
  }
  enum class PrevChar { NotChar, Space, NBSP };
  PrevChar prevChar = PrevChar::NotChar;
  uint32_t invisibleChars = 0u;
  for (const uint32_t offset : IntegerRange(aOffset, endOffset)) {
    if (characterDataBuffer.CharAt(offset) == HTMLEditUtils::kNBSP) {
      prevChar = PrevChar::NBSP;
      continue;
    }
    MOZ_ASSERT(
        EditorRawDOMPointInText(&aText, offset).IsCharCollapsibleASCIISpace());
    if (offset < firstVisibleOffset || offset >= afterLastVisibleOffset ||
        // white-space after another white-space is invisible
        prevChar == PrevChar::Space) {
      invisibleChars++;
    }
    prevChar = PrevChar::Space;
  }
  return invisibleChars;
}

bool HTMLEditUtils::IsEmptyNode(nsPresContext* aPresContext,
                                const nsINode& aNode,
                                const EmptyCheckOptions& aOptions /* = {} */,
                                bool* aSeenBR /* = nullptr */) {
  MOZ_ASSERT_IF(aOptions.contains(EmptyCheckOption::SafeToAskLayout),
                aPresContext);

  if (aSeenBR) {
    *aSeenBR = false;
  }

  if (const Text* text = Text::FromNode(&aNode)) {
    return aOptions.contains(EmptyCheckOption::SafeToAskLayout)
               ? !IsInVisibleTextFrames(aPresContext, *text)
               : !IsVisibleTextNode(
                     *text, aOptions.contains(
                                EmptyCheckOption::TreatSingleBRElementAsVisible)
                                ? TreatInvisibleLineBreakAs::Visible
                                : TreatInvisibleLineBreakAs::Invisible);
  }

  const bool treatCommentAsVisible =
      aOptions.contains(EmptyCheckOption::TreatCommentAsVisible);
  if (aNode.IsComment()) {
    return !treatCommentAsVisible;
  }

  if (!aNode.IsElement()) {
    return false;
  }

  if (
      // If it's not a container such as an <hr> or <br>, etc, it should be
      // treated as not empty.
      // XXX I think <input type="hidden"> should not be treated as a special
      // element since it's invisible. Treating invisible elements as special
      // ones causes changing the behavior with the invisible thing so that the
      // users may report the different behavior as a bug.
      !IsContainerNode(*aNode.AsContent()) ||
      // If it's a named anchor, we shouldn't treat it as empty because it
      // has special meaning even if invisible.
      IsNamedAnchorElement(*aNode.AsContent()) ||
      // Replaced elements should be treated as not empty because they have
      // visible content.
      IsReplacedElement(*aNode.AsElement())) {
    return false;
  }

  const auto [isListItem, isTableCell, hasAppearance] =
      [&]() MOZ_NEVER_INLINE_DEBUG -> std::tuple<bool, bool, bool> {
    // Let's stop treating the document element and the <body> as a list item
    // nor a table cell to avoid tricky cases.
    if (aNode.OwnerDoc()->GetDocumentElement() == &aNode ||
        (aNode.IsHTMLElement(nsGkAtoms::body) &&
         aNode.OwnerDoc()->GetBodyElement() == &aNode)) {
      return {false, false, false};
    }

    RefPtr<const ComputedStyle> elementStyle =
        nsComputedDOMStyle::GetComputedStyleNoFlush(aNode.AsElement());
    // If there is no style information like in a document fragment, let's refer
    // the default style.
    if (MOZ_UNLIKELY(!elementStyle)) {
      return {IsListItemElement(*aNode.AsContent()),
              IsTableCellElement(*aNode.AsContent()), false};
    }
    const nsStyleDisplay* styleDisplay = elementStyle->StyleDisplay();
    if (NS_WARN_IF(!styleDisplay)) {
      return {IsListItemElement(*aNode.AsContent()),
              IsTableCellElement(*aNode.AsContent()), false};
    }
    if (styleDisplay->mDisplay != StyleDisplay::None &&
        styleDisplay->HasNativeAppearance()) {
      return {false, false, true};
    }
    if (styleDisplay->IsListItem()) {
      return {true, false, false};
    }
    if (styleDisplay->mDisplay == StyleDisplay::TableCell) {
      return {false, true, false};
    }
    // The default display of <dt> and <dd> is block.  Therefore, we need
    // special handling for them.
    return {styleDisplay->mDisplay == StyleDisplay::Block &&
                aNode.IsAnyOfHTMLElements(nsGkAtoms::dd, nsGkAtoms::dt),
            false, false};
  }();

  // The web author created native widget without form control elements.  Let's
  // treat it as visible.
  if (hasAppearance) {
    return false;
  }

  if (isListItem &&
      aOptions.contains(EmptyCheckOption::TreatListItemAsVisible)) {
    return false;
  }
  if (isTableCell &&
      aOptions.contains(EmptyCheckOption::TreatTableCellAsVisible)) {
    return false;
  }

  const bool treatNonEditableContentAsInvisible =
      aOptions.contains(EmptyCheckOption::TreatNonEditableContentAsInvisible);
  bool seenBR = aSeenBR && *aSeenBR;
  for (nsIContent* childContent = aNode.GetFirstChild(); childContent;
       childContent = childContent->GetNextSibling()) {
    if (childContent->IsComment()) {
      if (treatCommentAsVisible) {
        return false;
      }
      continue;
    }
    if (treatNonEditableContentAsInvisible &&
        !HTMLEditUtils::IsSimplyEditableNode(*childContent)) {
      continue;
    }
    if (Text* text = Text::FromNode(childContent)) {
      // break out if we find we aren't empty
      if (aOptions.contains(EmptyCheckOption::SafeToAskLayout)
              ? IsInVisibleTextFrames(aPresContext, *text)
              : IsVisibleTextNode(
                    *text, aOptions.contains(
                               EmptyCheckOption::TreatSingleBRElementAsVisible)
                               ? TreatInvisibleLineBreakAs::Visible
                               : TreatInvisibleLineBreakAs::Invisible)) {
        return false;
      }
      continue;
    }

    if (childContent->IsComment()) {
      continue;
    }

    MOZ_ASSERT(childContent != &aNode);

    if (!aOptions.contains(EmptyCheckOption::TreatSingleBRElementAsVisible) &&
        !seenBR && childContent->IsHTMLElement(nsGkAtoms::br)) {
      // Ignore first <br> element in it if caller wants so because it's
      // typically a padding <br> element of for a parent block.
      seenBR = true;
      if (aSeenBR) {
        *aSeenBR = true;
      }
      continue;
    }

    if (aOptions.contains(EmptyCheckOption::TreatBlockAsVisible) &&
        HTMLEditUtils::IsBlockElement(
            *childContent, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      return false;
    }

    // Note: list items or table cells are not considered empty
    // if they contain other lists or tables
    EmptyCheckOptions options(aOptions);
    if (childContent->IsElement() && (isListItem || isTableCell)) {
      options += {EmptyCheckOption::TreatListItemAsVisible,
                  EmptyCheckOption::TreatTableCellAsVisible};
    }
    if (!IsEmptyNode(aPresContext, *childContent, options, &seenBR)) {
      if (aSeenBR) {
        *aSeenBR = seenBR;
      }
      return false;
    }
  }

  if (aSeenBR) {
    *aSeenBR = seenBR;
  }
  return true;
}

bool HTMLEditUtils::ShouldInsertLinefeedCharacter(
    const EditorDOMPoint& aPointToInsert, const Element& aEditingHost) {
  MOZ_ASSERT(aPointToInsert.IsSetAndValid());

  if (!aPointToInsert.IsInContentNode()) {
    return false;
  }

  // If in contenteditable=plaintext-only, we should use linefeed when it's
  // preformatted.
  if (aEditingHost.IsContentEditablePlainTextOnly()) {
    return EditorUtils::IsNewLinePreformatted(
        *aPointToInsert.ContainerAs<nsIContent>());
  }

  // closestEditableBlockElement can be nullptr if aEditingHost is an inline
  // element.
  Element* closestEditableBlockElement =
      HTMLEditUtils::GetInclusiveAncestorElement(
          *aPointToInsert.ContainerAs<nsIContent>(),
          HTMLEditUtils::ClosestEditableBlockElement,
          BlockInlineCheck::UseComputedDisplayOutsideStyle);

  // If and only if the nearest block is the editing host or its parent,
  // and new line character is preformatted, we should insert a linefeed.
  return (!closestEditableBlockElement ||
          closestEditableBlockElement == &aEditingHost) &&
         EditorUtils::IsNewLinePreformatted(
             *aPointToInsert.ContainerAs<nsIContent>());
}

// We use bitmasks to test containment of elements. Elements are marked to be
// in certain groups by setting the mGroup member of the `ElementInfo` struct
// to the corresponding GROUP_ values (OR'ed together). Similarly, elements are
// marked to allow containment of certain groups by setting the
// mCanContainGroups member of the `ElementInfo` struct to the corresponding
// GROUP_ values (OR'ed together).
// Testing containment then simply consists of checking whether the
// mCanContainGroups bitmask of an element and the mGroup bitmask of a
// potential child overlap.

#define GROUP_NONE 0

// body, head, html
#define GROUP_TOPLEVEL (1 << 1)

// base, link, meta, script, style, title
#define GROUP_HEAD_CONTENT (1 << 2)

// b, big, i, s, small, strike, tt, u
#define GROUP_FONTSTYLE (1 << 3)

// abbr, acronym, cite, code, datalist, del, dfn, em, ins, kbd, mark, rb, rp
// rt, rtc, ruby, samp, strong, var
#define GROUP_PHRASE (1 << 4)

// a, applet, basefont, bdi, bdo, br, font, iframe, img, map, meter, object,
// output, picture, progress, q, script, span, sub, sup
#define GROUP_SPECIAL (1 << 5)

// button, form, input, label, select, textarea
#define GROUP_FORMCONTROL (1 << 6)

// address, applet, article, aside, blockquote, button, center, del, details,
// dialog, dir, div, dl, fieldset, figure, footer, form, h1, h2, h3, h4, h5,
// h6, header, hgroup, hr, iframe, ins, main, map, menu, nav, noframes,
// noscript, object, ol, p, pre, table, search, section, summary, ul
#define GROUP_BLOCK (1 << 7)

// frame, frameset
#define GROUP_FRAME (1 << 8)

// col, tbody
#define GROUP_TABLE_CONTENT (1 << 9)

// tr
#define GROUP_TBODY_CONTENT (1 << 10)

// td, th
#define GROUP_TR_CONTENT (1 << 11)

// col
#define GROUP_COLGROUP_CONTENT (1 << 12)

// param
#define GROUP_OBJECT_CONTENT (1 << 13)

// li
#define GROUP_LI (1 << 14)

// area
#define GROUP_MAP_CONTENT (1 << 15)

// optgroup, option
#define GROUP_SELECT_CONTENT (1 << 16)

// option
#define GROUP_OPTIONS (1 << 17)

// dd, dt
#define GROUP_DL_CONTENT (1 << 18)

// p
#define GROUP_P (1 << 19)

// text, white-space, newline, comment
#define GROUP_LEAF (1 << 20)

// XXX This is because the editor does sublists illegally.
// ol, ul
#define GROUP_OL_UL (1 << 21)

// h1, h2, h3, h4, h5, h6
#define GROUP_HEADING (1 << 22)

// figcaption
#define GROUP_FIGCAPTION (1 << 23)

// picture members (img, source)
#define GROUP_PICTURE_CONTENT (1 << 24)

#define GROUP_INLINE_ELEMENT                                            \
  (GROUP_FONTSTYLE | GROUP_PHRASE | GROUP_SPECIAL | GROUP_FORMCONTROL | \
   GROUP_LEAF)

#define GROUP_FLOW_ELEMENT (GROUP_INLINE_ELEMENT | GROUP_BLOCK)

struct ElementInfo final {
#ifdef DEBUG
  nsHTMLTag mTag;
#endif
  // See `GROUP_NONE`'s comment.
  uint32_t mGroup;
  // See `GROUP_NONE`'s comment.
  uint32_t mCanContainGroups;
  bool mIsContainer;
  bool mCanContainSelf;
};

#ifdef DEBUG
#  define ELEM(_tag, _isContainer, _canContainSelf, _group, _canContainGroups) \
    {eHTMLTag_##_tag, _group, _canContainGroups, _isContainer, _canContainSelf}
#else
#  define ELEM(_tag, _isContainer, _canContainSelf, _group, _canContainGroups) \
    {_group, _canContainGroups, _isContainer, _canContainSelf}
#endif

static const ElementInfo kElements[eHTMLTag_userdefined] = {
    ELEM(a, true, false, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(abbr, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(acronym, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(address, true, true, GROUP_BLOCK, GROUP_INLINE_ELEMENT | GROUP_P),
    // While applet is no longer a valid tag, removing it here breaks the editor
    // (compiles, but causes many tests to fail in odd ways). This list is
    // tracked against the main HTML Tag list, so any changes will require more
    // than just removing entries.
    ELEM(applet, true, true, GROUP_SPECIAL | GROUP_BLOCK,
         GROUP_FLOW_ELEMENT | GROUP_OBJECT_CONTENT),
    ELEM(area, false, false, GROUP_MAP_CONTENT, GROUP_NONE),
    ELEM(article, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(aside, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(audio, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(b, true, true, GROUP_FONTSTYLE, GROUP_INLINE_ELEMENT),
    ELEM(base, false, false, GROUP_HEAD_CONTENT, GROUP_NONE),
    ELEM(basefont, false, false, GROUP_SPECIAL, GROUP_NONE),
    ELEM(bdi, true, true, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(bdo, true, true, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(bgsound, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(big, true, true, GROUP_FONTSTYLE, GROUP_INLINE_ELEMENT),
    ELEM(blockquote, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(body, true, true, GROUP_TOPLEVEL, GROUP_FLOW_ELEMENT),
    ELEM(br, false, false, GROUP_SPECIAL, GROUP_NONE),
    ELEM(button, true, true, GROUP_FORMCONTROL | GROUP_BLOCK,
         GROUP_FLOW_ELEMENT),
    ELEM(canvas, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(caption, true, true, GROUP_NONE, GROUP_INLINE_ELEMENT),
    ELEM(center, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(cite, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(code, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(col, false, false, GROUP_TABLE_CONTENT | GROUP_COLGROUP_CONTENT,
         GROUP_NONE),
    ELEM(colgroup, true, false, GROUP_NONE, GROUP_COLGROUP_CONTENT),
    ELEM(data, true, false, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(datalist, true, false, GROUP_PHRASE,
         GROUP_OPTIONS | GROUP_INLINE_ELEMENT),
    ELEM(dd, true, false, GROUP_DL_CONTENT, GROUP_FLOW_ELEMENT),
    ELEM(del, true, true, GROUP_PHRASE | GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(details, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(dfn, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(dialog, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(dir, true, false, GROUP_BLOCK, GROUP_LI),
    ELEM(div, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(dl, true, false, GROUP_BLOCK, GROUP_DL_CONTENT),
    ELEM(dt, true, true, GROUP_DL_CONTENT, GROUP_INLINE_ELEMENT),
    ELEM(em, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(embed, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(fieldset, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(figcaption, true, false, GROUP_FIGCAPTION, GROUP_FLOW_ELEMENT),
    ELEM(figure, true, true, GROUP_BLOCK,
         GROUP_FLOW_ELEMENT | GROUP_FIGCAPTION),
    ELEM(font, true, true, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(footer, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(form, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(frame, false, false, GROUP_FRAME, GROUP_NONE),
    ELEM(frameset, true, true, GROUP_FRAME, GROUP_FRAME),
    ELEM(h1, true, false, GROUP_BLOCK | GROUP_HEADING, GROUP_INLINE_ELEMENT),
    ELEM(h2, true, false, GROUP_BLOCK | GROUP_HEADING, GROUP_INLINE_ELEMENT),
    ELEM(h3, true, false, GROUP_BLOCK | GROUP_HEADING, GROUP_INLINE_ELEMENT),
    ELEM(h4, true, false, GROUP_BLOCK | GROUP_HEADING, GROUP_INLINE_ELEMENT),
    ELEM(h5, true, false, GROUP_BLOCK | GROUP_HEADING, GROUP_INLINE_ELEMENT),
    ELEM(h6, true, false, GROUP_BLOCK | GROUP_HEADING, GROUP_INLINE_ELEMENT),
    ELEM(head, true, false, GROUP_TOPLEVEL, GROUP_HEAD_CONTENT),
    ELEM(header, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(hgroup, true, false, GROUP_BLOCK, GROUP_HEADING),
    ELEM(hr, false, false, GROUP_BLOCK, GROUP_NONE),
    ELEM(html, true, false, GROUP_TOPLEVEL, GROUP_TOPLEVEL),
    ELEM(i, true, true, GROUP_FONTSTYLE, GROUP_INLINE_ELEMENT),
    ELEM(iframe, true, true, GROUP_SPECIAL | GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(image, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(img, false, false, GROUP_SPECIAL | GROUP_PICTURE_CONTENT, GROUP_NONE),
    ELEM(input, false, false, GROUP_FORMCONTROL, GROUP_NONE),
    ELEM(ins, true, true, GROUP_PHRASE | GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(kbd, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(keygen, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(label, true, false, GROUP_FORMCONTROL, GROUP_INLINE_ELEMENT),
    ELEM(legend, true, true, GROUP_NONE, GROUP_INLINE_ELEMENT),
    ELEM(li, true, false, GROUP_LI, GROUP_FLOW_ELEMENT),
    ELEM(link, false, false, GROUP_HEAD_CONTENT, GROUP_NONE),
    ELEM(listing, true, true, GROUP_BLOCK, GROUP_INLINE_ELEMENT),
    ELEM(main, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(map, true, true, GROUP_SPECIAL, GROUP_BLOCK | GROUP_MAP_CONTENT),
    ELEM(mark, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(marquee, true, false, GROUP_NONE, GROUP_NONE),
    ELEM(menu, true, true, GROUP_BLOCK, GROUP_LI | GROUP_FLOW_ELEMENT),
    ELEM(meta, false, false, GROUP_HEAD_CONTENT, GROUP_NONE),
    ELEM(meter, true, false, GROUP_SPECIAL, GROUP_FLOW_ELEMENT),
    ELEM(multicol, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(nav, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(nobr, true, false, GROUP_NONE, GROUP_NONE),
    ELEM(noembed, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(noframes, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(noscript, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(object, true, true, GROUP_SPECIAL | GROUP_BLOCK,
         GROUP_FLOW_ELEMENT | GROUP_OBJECT_CONTENT),
    // XXX Can contain self and ul because editor does sublists illegally.
    ELEM(ol, true, true, GROUP_BLOCK | GROUP_OL_UL, GROUP_LI | GROUP_OL_UL),
    ELEM(optgroup, true, false, GROUP_SELECT_CONTENT, GROUP_OPTIONS),
    ELEM(option, true, false, GROUP_SELECT_CONTENT | GROUP_OPTIONS, GROUP_LEAF),
    ELEM(output, true, true, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(p, true, false, GROUP_BLOCK | GROUP_P, GROUP_INLINE_ELEMENT),
    ELEM(param, false, false, GROUP_OBJECT_CONTENT, GROUP_NONE),
    ELEM(picture, true, false, GROUP_SPECIAL, GROUP_PICTURE_CONTENT),
    ELEM(plaintext, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(pre, true, true, GROUP_BLOCK, GROUP_INLINE_ELEMENT),
    ELEM(progress, true, false, GROUP_SPECIAL, GROUP_FLOW_ELEMENT),
    ELEM(q, true, true, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(rb, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(rp, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(rt, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(rtc, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(ruby, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(s, true, true, GROUP_FONTSTYLE, GROUP_INLINE_ELEMENT),
    ELEM(samp, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(script, true, false, GROUP_HEAD_CONTENT | GROUP_SPECIAL, GROUP_LEAF),
    ELEM(search, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(section, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(select, true, false, GROUP_FORMCONTROL, GROUP_SELECT_CONTENT),
    ELEM(selectedcontent, true, false, GROUP_NONE, GROUP_INLINE_ELEMENT),
    ELEM(small, true, true, GROUP_FONTSTYLE, GROUP_INLINE_ELEMENT),
    ELEM(slot, true, false, GROUP_NONE, GROUP_FLOW_ELEMENT),
    ELEM(source, false, false, GROUP_PICTURE_CONTENT, GROUP_NONE),
    ELEM(span, true, true, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(strike, true, true, GROUP_FONTSTYLE, GROUP_INLINE_ELEMENT),
    ELEM(strong, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(style, true, false, GROUP_HEAD_CONTENT, GROUP_LEAF),
    ELEM(sub, true, true, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(summary, true, true, GROUP_BLOCK, GROUP_FLOW_ELEMENT),
    ELEM(sup, true, true, GROUP_SPECIAL, GROUP_INLINE_ELEMENT),
    ELEM(table, true, false, GROUP_BLOCK, GROUP_TABLE_CONTENT),
    ELEM(tbody, true, false, GROUP_TABLE_CONTENT, GROUP_TBODY_CONTENT),
    ELEM(td, true, false, GROUP_TR_CONTENT, GROUP_FLOW_ELEMENT),
    ELEM(textarea, true, false, GROUP_FORMCONTROL, GROUP_LEAF),
    ELEM(tfoot, true, false, GROUP_NONE, GROUP_TBODY_CONTENT),
    ELEM(th, true, false, GROUP_TR_CONTENT, GROUP_FLOW_ELEMENT),
    ELEM(thead, true, false, GROUP_NONE, GROUP_TBODY_CONTENT),
    ELEM(template, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(time, true, false, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(title, true, false, GROUP_HEAD_CONTENT, GROUP_LEAF),
    ELEM(tr, true, false, GROUP_TBODY_CONTENT, GROUP_TR_CONTENT),
    ELEM(track, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(tt, true, true, GROUP_FONTSTYLE, GROUP_INLINE_ELEMENT),
    ELEM(u, true, true, GROUP_FONTSTYLE, GROUP_INLINE_ELEMENT),
    // XXX Can contain self and ol because editor does sublists illegally.
    ELEM(ul, true, true, GROUP_BLOCK | GROUP_OL_UL, GROUP_LI | GROUP_OL_UL),
    ELEM(var, true, true, GROUP_PHRASE, GROUP_INLINE_ELEMENT),
    ELEM(video, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(wbr, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(xmp, true, false, GROUP_BLOCK, GROUP_NONE),

    // These aren't elements.
    ELEM(text, false, false, GROUP_LEAF, GROUP_NONE),
    ELEM(whitespace, false, false, GROUP_LEAF, GROUP_NONE),
    ELEM(newline, false, false, GROUP_LEAF, GROUP_NONE),
    ELEM(comment, false, false, GROUP_LEAF, GROUP_NONE),
    ELEM(entity, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(doctypeDecl, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(markupDecl, false, false, GROUP_NONE, GROUP_NONE),
    ELEM(instruction, false, false, GROUP_NONE, GROUP_NONE),

    ELEM(userdefined, true, false, GROUP_NONE, GROUP_FLOW_ELEMENT)};

bool HTMLEditUtils::CanNodeContain(nsHTMLTag aParentTagId,
                                   nsHTMLTag aChildTagId) {
  NS_ASSERTION(
      aParentTagId > eHTMLTag_unknown && aParentTagId <= eHTMLTag_userdefined,
      "aParentTagId out of range!");
  NS_ASSERTION(
      aChildTagId > eHTMLTag_unknown && aChildTagId <= eHTMLTag_userdefined,
      "aChildTagId out of range!");

#ifdef DEBUG
  static bool checked = false;
  if (!checked) {
    checked = true;
    int32_t i;
    for (i = 1; i <= eHTMLTag_userdefined; ++i) {
      NS_ASSERTION(kElements[i - 1].mTag == i,
                   "You need to update kElements (missing tags).");
    }
  }
#endif

  // Special-case button.
  if (aParentTagId == eHTMLTag_button) {
    static const nsHTMLTag kButtonExcludeKids[] = {
        eHTMLTag_a,     eHTMLTag_fieldset, eHTMLTag_form,    eHTMLTag_iframe,
        eHTMLTag_input, eHTMLTag_select,   eHTMLTag_textarea};

    uint32_t j;
    for (j = 0; j < std::size(kButtonExcludeKids); ++j) {
      if (kButtonExcludeKids[j] == aChildTagId) {
        return false;
      }
    }
  }

  // Deprecated elements.
  if (aChildTagId == eHTMLTag_bgsound) {
    return false;
  }

  // Bug #67007, dont strip userdefined tags.
  if (aChildTagId == eHTMLTag_userdefined) {
    return true;
  }

  const ElementInfo& parent = kElements[aParentTagId - 1];
  if (aParentTagId == aChildTagId) {
    return parent.mCanContainSelf;
  }

  const ElementInfo& child = kElements[aChildTagId - 1];
  return !!(parent.mCanContainGroups & child.mGroup);
}

bool HTMLEditUtils::ContentIsInert(const nsIContent& aContent) {
  for (nsIContent* content :
       aContent.InclusiveFlatTreeAncestorsOfType<nsIContent>()) {
    if (nsIFrame* frame = content->GetPrimaryFrame()) {
      return frame->StyleUI()->IsInert();
    }
    // If it doesn't have primary frame, we need to check its ancestors.
    // This may occur if it's an invisible text node or element nodes whose
    // display is an invisible value.
    if (!content->IsElement()) {
      continue;
    }
    if (content->AsElement()->State().HasState(dom::ElementState::INERT)) {
      return true;
    }
  }
  return false;
}

bool HTMLEditUtils::IsContainerNode(nsHTMLTag aTagId) {
  NS_ASSERTION(aTagId > eHTMLTag_unknown && aTagId <= eHTMLTag_userdefined,
               "aTagId out of range!");

  return kElements[aTagId - 1].mIsContainer;
}

bool HTMLEditUtils::IsNonListSingleLineContainer(const nsIContent& aContent) {
  return aContent.IsAnyOfHTMLElements(
      nsGkAtoms::address, nsGkAtoms::div, nsGkAtoms::h1, nsGkAtoms::h2,
      nsGkAtoms::h3, nsGkAtoms::h4, nsGkAtoms::h5, nsGkAtoms::h6,
      nsGkAtoms::listing, nsGkAtoms::p, nsGkAtoms::pre, nsGkAtoms::xmp);
}

bool HTMLEditUtils::IsSingleLineContainer(const nsIContent& aContent) {
  return IsNonListSingleLineContainer(aContent) ||
         aContent.IsAnyOfHTMLElements(nsGkAtoms::li, nsGkAtoms::dt,
                                      nsGkAtoms::dd);
}

// static
template <typename EditorDOMPointType>
EditorDOMPointType HTMLEditUtils::GetPreviousEditablePoint(
    nsIContent& aContent, const Element* aAncestorLimiter,
    InvisibleWhiteSpaces aInvisibleWhiteSpaces,
    TableBoundary aHowToTreatTableBoundary) {
  MOZ_ASSERT(HTMLEditUtils::IsSimplyEditableNode(aContent));
  NS_ASSERTION(!HTMLEditUtils::IsAnyTableElementExceptColumnElement(aContent) ||
                   HTMLEditUtils::IsTableCellOrCaptionElement(aContent),
               "HTMLEditUtils::GetPreviousEditablePoint() may return a point "
               "between table structure elements");

  if (&aContent == aAncestorLimiter) {
    return EditorDOMPointType();
  }

  // First, look for previous content.
  nsIContent* previousContent = aContent.GetPreviousSibling();
  if (!previousContent) {
    if (!aContent.GetParentElement()) {
      return EditorDOMPointType();
    }
    nsIContent* inclusiveAncestor = &aContent;
    for (Element* const parentElement : aContent.AncestorsOfType<Element>()) {
      if (parentElement == aAncestorLimiter ||
          !HTMLEditUtils::IsSimplyEditableNode(*parentElement) ||
          !HTMLEditUtils::CanCrossContentBoundary(*parentElement,
                                                  aHowToTreatTableBoundary)) {
        // If cannot cross the parent element boundary, return the point of
        // last inclusive ancestor point.
        return EditorDOMPointType(inclusiveAncestor);
      }

      // Start of the parent element is a next editable point if it's an
      // element which is not a table structure element.
      if (!HTMLEditUtils::IsAnyTableElementExceptColumnElement(
              *parentElement) ||
          HTMLEditUtils::IsTableCellOrCaptionElement(*parentElement)) {
        inclusiveAncestor = parentElement;
      }

      previousContent = parentElement->GetPreviousSibling();
      if (!previousContent) {
        continue;  // Keep looking for previous sibling of an ancestor.
      }

      // XXX Should we ignore data node like CDATA, Comment, etc?

      // If previous content is not editable, let's return the point after it.
      if (!HTMLEditUtils::IsSimplyEditableNode(*previousContent)) {
        return EditorDOMPointType::After(*previousContent);
      }

      // If cannot cross previous content boundary, return start of last
      // inclusive ancestor.
      if (!HTMLEditUtils::CanCrossContentBoundary(*previousContent,
                                                  aHowToTreatTableBoundary)) {
        return inclusiveAncestor == &aContent
                   ? EditorDOMPointType(inclusiveAncestor)
                   : EditorDOMPointType(inclusiveAncestor, 0);
      }
      break;
    }
    if (!previousContent) {
      return EditorDOMPointType(inclusiveAncestor);
    }
  } else if (!HTMLEditUtils::IsSimplyEditableNode(*previousContent)) {
    return EditorDOMPointType::After(*previousContent);
  } else if (!HTMLEditUtils::CanCrossContentBoundary(
                 *previousContent, aHowToTreatTableBoundary)) {
    return EditorDOMPointType(&aContent);
  }

  // Next, look for end of the previous content.
  nsIContent* leafContent = previousContent;
  if (previousContent->GetChildCount() &&
      HTMLEditUtils::IsContainerNode(*previousContent)) {
    for (nsIContent* maybeLeafContent = previousContent->GetLastChild();
         maybeLeafContent;
         maybeLeafContent = maybeLeafContent->GetLastChild()) {
      // If it's not an editable content or cannot cross the boundary,
      // return the point after the content.  Note that in this case,
      // the content must not be any table elements except `<table>`
      // because we've climbed down the tree.
      if (!HTMLEditUtils::IsSimplyEditableNode(*maybeLeafContent) ||
          !HTMLEditUtils::CanCrossContentBoundary(*maybeLeafContent,
                                                  aHowToTreatTableBoundary)) {
        return EditorDOMPointType::After(*maybeLeafContent);
      }
      leafContent = maybeLeafContent;
      if (!HTMLEditUtils::IsContainerNode(*leafContent)) {
        break;
      }
    }
  }

  if (leafContent->IsText()) {
    Text* textNode = leafContent->AsText();
    if (aInvisibleWhiteSpaces == InvisibleWhiteSpaces::Preserve) {
      return EditorDOMPointType::AtEndOf(*textNode);
    }
    // There may be invisible trailing white-spaces which should be
    // ignored.  Let's scan its start.
    return WSRunScanner::GetAfterLastVisiblePoint<EditorDOMPointType>(
        {WSRunScanner::Option::OnlyEditableNodes}, *textNode);
  }

  // If it's a container element, return end of it.  Otherwise, return
  // the point after the non-container element.
  return HTMLEditUtils::IsContainerNode(*leafContent)
             ? EditorDOMPointType::AtEndOf(*leafContent)
             : EditorDOMPointType::After(*leafContent);
}

// static
template <typename EditorDOMPointType>
EditorDOMPointType HTMLEditUtils::GetNextEditablePoint(
    nsIContent& aContent, const Element* aAncestorLimiter,
    InvisibleWhiteSpaces aInvisibleWhiteSpaces,
    TableBoundary aHowToTreatTableBoundary) {
  MOZ_ASSERT(HTMLEditUtils::IsSimplyEditableNode(aContent));
  NS_ASSERTION(!HTMLEditUtils::IsAnyTableElementExceptColumnElement(aContent) ||
                   HTMLEditUtils::IsTableCellOrCaptionElement(aContent),
               "HTMLEditUtils::GetPreviousEditablePoint() may return a point "
               "between table structure elements");

  if (&aContent == aAncestorLimiter) {
    return EditorDOMPointType();
  }

  // First, look for next content.
  nsIContent* nextContent = aContent.GetNextSibling();
  if (!nextContent) {
    if (!aContent.GetParentElement()) {
      return EditorDOMPointType();
    }
    nsIContent* inclusiveAncestor = &aContent;
    for (Element* const parentElement : aContent.AncestorsOfType<Element>()) {
      if (parentElement == aAncestorLimiter ||
          !HTMLEditUtils::IsSimplyEditableNode(*parentElement) ||
          !HTMLEditUtils::CanCrossContentBoundary(*parentElement,
                                                  aHowToTreatTableBoundary)) {
        // If cannot cross the parent element boundary, return the point of
        // last inclusive ancestor point.
        return EditorDOMPointType(inclusiveAncestor);
      }

      // End of the parent element is a next editable point if it's an
      // element which is not a table structure element.
      if (!HTMLEditUtils::IsAnyTableElementExceptColumnElement(
              *parentElement) ||
          HTMLEditUtils::IsTableCellOrCaptionElement(*parentElement)) {
        inclusiveAncestor = parentElement;
      }

      nextContent = parentElement->GetNextSibling();
      if (!nextContent) {
        continue;  // Keep looking for next sibling of an ancestor.
      }

      // XXX Should we ignore data node like CDATA, Comment, etc?

      // If next content is not editable, let's return the point after
      // the last inclusive ancestor.
      if (!HTMLEditUtils::IsSimplyEditableNode(*nextContent)) {
        return EditorDOMPointType::After(*parentElement);
      }

      // If cannot cross next content boundary, return after the last
      // inclusive ancestor.
      if (!HTMLEditUtils::CanCrossContentBoundary(*nextContent,
                                                  aHowToTreatTableBoundary)) {
        return EditorDOMPointType::After(*inclusiveAncestor);
      }
      break;
    }
    if (!nextContent) {
      return EditorDOMPointType::After(*inclusiveAncestor);
    }
  } else if (!HTMLEditUtils::IsSimplyEditableNode(*nextContent)) {
    return EditorDOMPointType::After(aContent);
  } else if (!HTMLEditUtils::CanCrossContentBoundary(
                 *nextContent, aHowToTreatTableBoundary)) {
    return EditorDOMPointType::After(aContent);
  }

  // Next, look for start of the next content.
  nsIContent* leafContent = nextContent;
  if (nextContent->GetChildCount() &&
      HTMLEditUtils::IsContainerNode(*nextContent)) {
    for (nsIContent* maybeLeafContent = nextContent->GetFirstChild();
         maybeLeafContent;
         maybeLeafContent = maybeLeafContent->GetFirstChild()) {
      // If it's not an editable content or cannot cross the boundary,
      // return the point at the content (i.e., start of its parent).  Note
      // that in this case, the content must not be any table elements except
      // `<table>` because we've climbed down the tree.
      if (!HTMLEditUtils::IsSimplyEditableNode(*maybeLeafContent) ||
          !HTMLEditUtils::CanCrossContentBoundary(*maybeLeafContent,
                                                  aHowToTreatTableBoundary)) {
        return EditorDOMPointType(maybeLeafContent);
      }
      leafContent = maybeLeafContent;
      if (!HTMLEditUtils::IsContainerNode(*leafContent)) {
        break;
      }
    }
  }

  if (leafContent->IsText()) {
    Text* textNode = leafContent->AsText();
    if (aInvisibleWhiteSpaces == InvisibleWhiteSpaces::Preserve) {
      return EditorDOMPointType(textNode, 0);
    }
    // There may be invisible leading white-spaces which should be
    // ignored.  Let's scan its start.
    return WSRunScanner::GetFirstVisiblePoint<EditorDOMPointType>(
        {WSRunScanner::Option::OnlyEditableNodes}, *textNode);
  }

  // If it's a container element, return start of it.  Otherwise, return
  // the point at the non-container element (i.e., start of its parent).
  return HTMLEditUtils::IsContainerNode(*leafContent)
             ? EditorDOMPointType(leafContent, 0)
             : EditorDOMPointType(leafContent);
}

// static
Element* HTMLEditUtils::GetAncestorElement(
    const nsIContent& aContent, const AncestorTypes& aAncestorTypes,
    BlockInlineCheck aBlockInlineCheck,
    const Element* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT(
      aAncestorTypes.contains(AncestorType::ClosestBlockElement) ||
      aAncestorTypes.contains(AncestorType::ClosestContainerElement) ||
      aAncestorTypes.contains(AncestorType::MostDistantInlineElementInBlock) ||
      aAncestorTypes.contains(AncestorType::ClosestButtonElement) ||
      aAncestorTypes.contains(
          AncestorType::ReturnAncestorLimiterIfNoProperAncestor));
  MOZ_ASSERT_IF(
      aAncestorTypes.contains(AncestorType::ClosestBlockElement),
      !aAncestorTypes.contains(AncestorType::MostDistantInlineElementInBlock));
  MOZ_ASSERT_IF(
      aAncestorTypes.contains(AncestorType::ClosestContainerElement),
      !aAncestorTypes.contains(AncestorType::MostDistantInlineElementInBlock));
  MOZ_ASSERT_IF(
      aAncestorTypes.contains(AncestorType::StopAtClosestButtonElement),
      !aAncestorTypes.contains(AncestorType::ClosestButtonElement));
  MOZ_ASSERT_IF(
      aAncestorTypes.contains(AncestorType::ClosestButtonElement),
      !aAncestorTypes.contains(AncestorType::StopAtClosestButtonElement));

  aBlockInlineCheck = UseComputedDisplayStyleIfAuto(aBlockInlineCheck);

  const Element* theBodyElement = aContent.OwnerDoc()->GetBody();
  const Element* theDocumentElement = aContent.OwnerDoc()->GetDocumentElement();
  Element* lastAncestorElement = nullptr;
  const bool editableElementOnly =
      aAncestorTypes.contains(AncestorType::EditableElement);
  const bool lookingForClosestBlockElement =
      aAncestorTypes.contains(AncestorType::ClosestBlockElement);
  const bool lookingForClosestContainerElement =
      aAncestorTypes.contains(AncestorType::ClosestContainerElement);
  const bool lookingForMostDistantInlineElementInBlock =
      aAncestorTypes.contains(AncestorType::MostDistantInlineElementInBlock);
  const bool stopAtClosestBlockElement =
      lookingForClosestBlockElement ||
      lookingForMostDistantInlineElementInBlock;
  const bool fallbackToLimiter = aAncestorTypes.contains(
      AncestorType::ReturnAncestorLimiterIfNoProperAncestor);
  const bool stopAtButton =
      aAncestorTypes.contains(AncestorType::StopAtClosestButtonElement);
  const bool lookingForButtonElement =
      aAncestorTypes.contains(AncestorType::ClosestButtonElement);
  const bool ignoreHRElement =
      aAncestorTypes.contains(AncestorType::IgnoreHRElement);
  const auto IsLimiter = [&](const nsIContent& aContent) -> bool {
    return &aContent == aAncestorLimiter ||
           // If aContent is the body element or the document element, we
           // shouldn't climb up to its parent.
           (editableElementOnly &&
            (&aContent == theBodyElement || &aContent == theDocumentElement ||
             aContent.IsEditingHost()));
  };
  const auto IsSearchingElementTypeExceptFallbackToRoot =
      [&](const nsIContent& aContent) -> bool {
    if (!aContent.IsElement() ||
        (ignoreHRElement && aContent.IsHTMLElement(nsGkAtoms::hr))) {
      return false;
    }
    if (editableElementOnly &&
        !EditorUtils::IsEditableContent(aContent, EditorType::HTML)) {
      return false;
    }
    return (lookingForClosestBlockElement &&
            HTMLEditUtils::IsBlockElement(aContent, aBlockInlineCheck)) ||
           (lookingForClosestContainerElement && aContent.IsElement() &&
            HTMLEditUtils::IsContainerNode(aContent)) ||
           (lookingForMostDistantInlineElementInBlock &&
            HTMLEditUtils::IsInlineContent(aContent, aBlockInlineCheck)) ||
           (lookingForButtonElement &&
            aContent.IsHTMLElement(nsGkAtoms::button));
  };
  if (IsLimiter(aContent)) {
    return nullptr;
  }
  for (Element* element : aContent.AncestorsOfType<Element>()) {
    if (editableElementOnly &&
        !EditorUtils::IsEditableContent(*element, EditorType::HTML)) {
      return lastAncestorElement;  // editing host (can be inline element)
    }
    if (ignoreHRElement && element->IsHTMLElement(nsGkAtoms::hr)) {
      if (IsLimiter(*element)) {
        if (fallbackToLimiter && !lastAncestorElement) {
          lastAncestorElement = element;
        }
        return lastAncestorElement;
      }
      continue;
    }
    if (stopAtButton && element->IsHTMLElement(nsGkAtoms::button)) {
      return lastAncestorElement;
    }
    if (lookingForButtonElement && element->IsHTMLElement(nsGkAtoms::button)) {
      return element;  // closest button element
    }
    if (lookingForClosestContainerElement &&
        HTMLEditUtils::IsContainerNode(*element)) {
      return element;  // closest container element
    }
    if (stopAtClosestBlockElement &&
        HTMLEditUtils::IsBlockElement(*element, aBlockInlineCheck)) {
      if (lookingForClosestBlockElement) {
        return element;  // closest block element
      }
      MOZ_ASSERT_IF(lastAncestorElement,
                    HTMLEditUtils::IsInlineContent(*lastAncestorElement,
                                                   aBlockInlineCheck));
      if (!lastAncestorElement && fallbackToLimiter && IsLimiter(*element)) {
        return element;  // closest block element and a limiter
      }
      return lastAncestorElement;  // the last inline element which we found
    }
    if (IsSearchingElementTypeExceptFallbackToRoot(*element)) {
      lastAncestorElement = element;
    }
    if (IsLimiter(*element)) {
      if (fallbackToLimiter && !lastAncestorElement) {
        lastAncestorElement = element;
      }
      return lastAncestorElement;
    }
  }
  return lastAncestorElement;
}

// static
Element* HTMLEditUtils::GetInclusiveAncestorElement(
    const nsIContent& aContent, const AncestorTypes& aAncestorTypes,
    BlockInlineCheck aBlockInlineCheck,
    const Element* aAncestorLimiter /* = nullptr */) {
  MOZ_ASSERT(
      aAncestorTypes.contains(AncestorType::ClosestBlockElement) ||
      aAncestorTypes.contains(AncestorType::ClosestContainerElement) ||
      aAncestorTypes.contains(AncestorType::MostDistantInlineElementInBlock) ||
      aAncestorTypes.contains(AncestorType::ClosestButtonElement) ||
      aAncestorTypes.contains(
          AncestorType::ReturnAncestorLimiterIfNoProperAncestor));
  MOZ_ASSERT_IF(
      aAncestorTypes.contains(AncestorType::ClosestBlockElement),
      !aAncestorTypes.contains(AncestorType::MostDistantInlineElementInBlock));
  MOZ_ASSERT_IF(
      aAncestorTypes.contains(AncestorType::ClosestContainerElement),
      !aAncestorTypes.contains(AncestorType::MostDistantInlineElementInBlock));
  MOZ_ASSERT_IF(
      aAncestorTypes.contains(AncestorType::StopAtClosestButtonElement),
      !aAncestorTypes.contains(AncestorType::ClosestButtonElement));
  MOZ_ASSERT_IF(
      aAncestorTypes.contains(AncestorType::ClosestButtonElement),
      !aAncestorTypes.contains(AncestorType::StopAtClosestButtonElement));

  aBlockInlineCheck = UseComputedDisplayStyleIfAuto(aBlockInlineCheck);

  const Element* theBodyElement = aContent.OwnerDoc()->GetBody();
  const Element* theDocumentElement = aContent.OwnerDoc()->GetDocumentElement();
  const bool editableElementOnly =
      aAncestorTypes.contains(AncestorType::EditableElement);
  const bool lookingForClosestBlockElement =
      aAncestorTypes.contains(AncestorType::ClosestBlockElement);
  const bool lookingForClosestContainerElement =
      aAncestorTypes.contains(AncestorType::ClosestContainerElement);
  const bool lookingForMostDistantInlineElementInBlock =
      aAncestorTypes.contains(AncestorType::MostDistantInlineElementInBlock);
  const bool stopAtClosestBlockElement =
      lookingForClosestBlockElement ||
      lookingForMostDistantInlineElementInBlock;
  const bool stopAtButton =
      aAncestorTypes.contains(AncestorType::StopAtClosestButtonElement);
  const bool lookingForButtonElement =
      aAncestorTypes.contains(AncestorType::ClosestButtonElement);
  const bool ignoreHRElement =
      aAncestorTypes.contains(AncestorType::IgnoreHRElement);
  const bool fallbackToLimiter = aAncestorTypes.contains(
      AncestorType::ReturnAncestorLimiterIfNoProperAncestor);
  const bool lookingForMostDistantElement =
      lookingForMostDistantInlineElementInBlock;
  const auto IsLimiter = [&](const nsIContent& aContent) -> bool {
    return &aContent == aAncestorLimiter || !aContent.GetParent() ||
           // If aContent is the body element or the document element, we
           // shouldn't climb up to its parent.
           (editableElementOnly &&
            (&aContent == theBodyElement || &aContent == theDocumentElement ||
             aContent.IsEditingHost()));
  };
  const auto IsSearchingElementTypeExceptFallbackToRoot =
      [&](const nsIContent& aContent) -> bool {
    if (!aContent.IsElement() ||
        (ignoreHRElement && aContent.IsHTMLElement(nsGkAtoms::hr))) {
      return false;
    }
    if (editableElementOnly &&
        !EditorUtils::IsEditableContent(aContent, EditorType::HTML)) {
      return false;
    }
    return (lookingForClosestBlockElement &&
            HTMLEditUtils::IsBlockElement(aContent, aBlockInlineCheck)) ||
           (lookingForClosestContainerElement && aContent.IsElement() &&
            HTMLEditUtils::IsContainerNode(aContent)) ||
           (lookingForMostDistantInlineElementInBlock &&
            HTMLEditUtils::IsInlineContent(aContent, aBlockInlineCheck)) ||
           (lookingForButtonElement &&
            aContent.IsHTMLElement(nsGkAtoms::button));
  };

  if (IsLimiter(aContent)) {
    return fallbackToLimiter ||
                   IsSearchingElementTypeExceptFallbackToRoot(aContent)
               ? const_cast<Element*>(aContent.AsElement())
               : nullptr;
  }

  if (stopAtButton && aContent.IsHTMLElement(nsGkAtoms::button)) {
    return IsSearchingElementTypeExceptFallbackToRoot(aContent)
               ? const_cast<Element*>(aContent.AsElement())
               : nullptr;
  }

  if (lookingForButtonElement && aContent.IsHTMLElement(nsGkAtoms::button)) {
    return const_cast<Element*>(aContent.AsElement());
  }

  if (lookingForClosestContainerElement && aContent.IsElement() &&
      HTMLEditUtils::IsContainerNode(aContent)) {
    return IsSearchingElementTypeExceptFallbackToRoot(aContent)
               ? const_cast<Element*>(aContent.AsElement())
               : nullptr;
  }

  // If aContent is a block element, we don't need to climb up the tree.
  // Consider the result right now.
  if (stopAtClosestBlockElement &&
      HTMLEditUtils::IsBlockElement(aContent, aBlockInlineCheck) &&
      !(ignoreHRElement && aContent.IsHTMLElement(nsGkAtoms::hr))) {
    return IsSearchingElementTypeExceptFallbackToRoot(aContent)
               ? const_cast<Element*>(aContent.AsElement())
               : nullptr;
  }

  Element* const result = HTMLEditUtils::GetAncestorElement(
      aContent, aAncestorTypes, aBlockInlineCheck, aAncestorLimiter);
  // If we're looking for the most distant ancestor of a type and there is no
  // such ancestor, aContent may be the most distant inclusive ancestor of the
  // type.
  if (lookingForMostDistantElement &&
      (!result || (result != &aContent && IsLimiter(*result) &&
                   !IsSearchingElementTypeExceptFallbackToRoot(*result))) &&
      IsSearchingElementTypeExceptFallbackToRoot(aContent)) {
    return const_cast<Element*>(aContent.AsElement());
  }
  return result;
}

// static
Element* HTMLEditUtils::GetClosestAncestorAnyListElement(
    const nsIContent& aContent) {
  for (Element* const element : aContent.AncestorsOfType<Element>()) {
    if (HTMLEditUtils::IsListElement(*element)) {
      return element;
    }
  }
  return nullptr;
}

// static
Element* HTMLEditUtils::GetClosestInclusiveAncestorAnyListElement(
    const nsIContent& aContent) {
  for (Element* const element : aContent.InclusiveAncestorsOfType<Element>()) {
    if (HTMLEditUtils::IsListElement(*element)) {
      return element;
    }
  }
  return nullptr;
}

EditAction HTMLEditUtils::GetEditActionForInsert(const nsAtom& aTagName) {
  // This method may be in a hot path.  So, return only necessary
  // EditAction::eInsert*Element.
  if (&aTagName == nsGkAtoms::ul) {
    // For InputEvent.inputType, "insertUnorderedList".
    return EditAction::eInsertUnorderedListElement;
  }
  if (&aTagName == nsGkAtoms::ol) {
    // For InputEvent.inputType, "insertOrderedList".
    return EditAction::eInsertOrderedListElement;
  }
  if (&aTagName == nsGkAtoms::hr) {
    // For InputEvent.inputType, "insertHorizontalRule".
    return EditAction::eInsertHorizontalRuleElement;
  }
  return EditAction::eInsertNode;
}

EditAction HTMLEditUtils::GetEditActionForRemoveList(const nsAtom& aTagName) {
  // This method may be in a hot path.  So, return only necessary
  // EditAction::eRemove*Element.
  if (&aTagName == nsGkAtoms::ul) {
    // For InputEvent.inputType, "insertUnorderedList".
    return EditAction::eRemoveUnorderedListElement;
  }
  if (&aTagName == nsGkAtoms::ol) {
    // For InputEvent.inputType, "insertOrderedList".
    return EditAction::eRemoveOrderedListElement;
  }
  return EditAction::eRemoveListElement;
}

EditAction HTMLEditUtils::GetEditActionForInsert(const Element& aElement) {
  return GetEditActionForInsert(*aElement.NodeInfo()->NameAtom());
}

EditAction HTMLEditUtils::GetEditActionForFormatText(const nsAtom& aProperty,
                                                     const nsAtom* aAttribute,
                                                     bool aToSetStyle) {
  // This method may be in a hot path.  So, return only necessary
  // EditAction::eSet*Property or EditAction::eRemove*Property.
  if (&aProperty == nsGkAtoms::b) {
    return aToSetStyle ? EditAction::eSetFontWeightProperty
                       : EditAction::eRemoveFontWeightProperty;
  }
  if (&aProperty == nsGkAtoms::i) {
    return aToSetStyle ? EditAction::eSetTextStyleProperty
                       : EditAction::eRemoveTextStyleProperty;
  }
  if (&aProperty == nsGkAtoms::u) {
    return aToSetStyle ? EditAction::eSetTextDecorationPropertyUnderline
                       : EditAction::eRemoveTextDecorationPropertyUnderline;
  }
  if (&aProperty == nsGkAtoms::strike) {
    return aToSetStyle ? EditAction::eSetTextDecorationPropertyLineThrough
                       : EditAction::eRemoveTextDecorationPropertyLineThrough;
  }
  if (&aProperty == nsGkAtoms::sup) {
    return aToSetStyle ? EditAction::eSetVerticalAlignPropertySuper
                       : EditAction::eRemoveVerticalAlignPropertySuper;
  }
  if (&aProperty == nsGkAtoms::sub) {
    return aToSetStyle ? EditAction::eSetVerticalAlignPropertySub
                       : EditAction::eRemoveVerticalAlignPropertySub;
  }
  if (&aProperty == nsGkAtoms::font) {
    if (aAttribute == nsGkAtoms::face) {
      return aToSetStyle ? EditAction::eSetFontFamilyProperty
                         : EditAction::eRemoveFontFamilyProperty;
    }
    if (aAttribute == nsGkAtoms::color) {
      return aToSetStyle ? EditAction::eSetColorProperty
                         : EditAction::eRemoveColorProperty;
    }
    if (aAttribute == nsGkAtoms::bgcolor) {
      return aToSetStyle ? EditAction::eSetBackgroundColorPropertyInline
                         : EditAction::eRemoveBackgroundColorPropertyInline;
    }
  }
  return aToSetStyle ? EditAction::eSetInlineStyleProperty
                     : EditAction::eRemoveInlineStyleProperty;
}

EditAction HTMLEditUtils::GetEditActionForAlignment(
    const nsAString& aAlignType) {
  // This method may be in a hot path.  So, return only necessary
  // EditAction::eAlign*.
  if (aAlignType.EqualsLiteral("left")) {
    return EditAction::eAlignLeft;
  }
  if (aAlignType.EqualsLiteral("right")) {
    return EditAction::eAlignRight;
  }
  if (aAlignType.EqualsLiteral("center")) {
    return EditAction::eAlignCenter;
  }
  if (aAlignType.EqualsLiteral("justify")) {
    return EditAction::eJustify;
  }
  return EditAction::eSetAlignment;
}

// static
template <typename EditorDOMPointType>
nsIContent* HTMLEditUtils::GetContentToPreserveInlineStyles(
    const EditorDOMPointType& aPoint, const Element& aEditingHost) {
  MOZ_ASSERT(aPoint.IsSetAndValid());
  if (MOZ_UNLIKELY(!aPoint.IsInContentNode())) {
    return nullptr;
  }
  // If it points middle of a text node, use it.  Otherwise, scan next visible
  // thing and use the style of following text node if there is.
  if (aPoint.IsInTextNode() && !aPoint.IsEndOfContainer()) {
    return aPoint.template ContainerAs<nsIContent>();
  }
  for (auto point = aPoint.template To<EditorRawDOMPoint>(); point.IsSet();) {
    const WSScanResult nextVisibleThing =
        WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes}, point);
    if (nextVisibleThing.InVisibleOrCollapsibleCharacters()) {
      return nextVisibleThing.TextPtr();
    }
    if (nextVisibleThing.ContentIsEditableRoot()) {
      break;
    }
    // Ignore invisible empty inline container elements because it's not visible
    // for users so that using the style will appear suddenly from point of view
    // of users.
    if (nextVisibleThing.ReachedEditableInvisibleEmptyInlineContainerElement(
            &aEditingHost)) {
      point.SetAfter(nextVisibleThing.ElementPtr());
      continue;
    }
    // Otherwise, we should use style of the container of the start point.
    break;
  }
  return aPoint.template ContainerAs<nsIContent>();
}

template <typename EditorDOMPointType, typename EditorDOMPointTypeInput>
EditorDOMPointType HTMLEditUtils::GetBetterInsertionPointFor(
    const nsIContent& aContentToInsert,
    const EditorDOMPointTypeInput& aPointToInsert) {
  if (NS_WARN_IF(!aPointToInsert.IsSet())) {
    return EditorDOMPointType();
  }

  auto pointToInsert =
      aPointToInsert.template GetNonAnonymousSubtreePoint<EditorDOMPointType>();
  if (NS_WARN_IF(!pointToInsert.IsSet()) ||
      NS_WARN_IF(!HTMLEditUtils::IsSimplyEditableNode(
          *pointToInsert.GetContainer()))) {
    // Cannot insert aContentToInsert into this DOM tree.
    return EditorDOMPointType();
  }

  // If the node to insert is not a block level element, we can insert it
  // at any point.
  if (!HTMLEditUtils::IsBlockElement(
          aContentToInsert, BlockInlineCheck::UseComputedDisplayStyle)) {
    return pointToInsert;
  }

  const WSRunScanner wsScannerForPointToInsert(
      {WSRunScanner::Option::OnlyEditableNodes}, pointToInsert);

  // If the insertion position is after the last visible item in a line,
  // i.e., the insertion position is just before a visible line break <br>,
  // we want to skip to the position just after the line break (see bug 68767).
  const WSScanResult forwardScanFromPointToInsertResult =
      wsScannerForPointToInsert.ScanInclusiveNextVisibleNodeOrBlockBoundaryFrom(
          pointToInsert);
  // So, if the next visible node isn't a <br> element, we can insert the block
  // level element to the point.
  if (!forwardScanFromPointToInsertResult.ReachedBRElement()) {
    return pointToInsert;
  }

  // However, we must not skip next <br> element when the caret appears to be
  // positioned at the beginning of a block, in that case skipping the <br>
  // would not insert the <br> at the caret position, but after the current
  // empty line.
  const WSScanResult backwardScanFromPointToInsertResult =
      wsScannerForPointToInsert.ScanPreviousVisibleNodeOrBlockBoundaryFrom(
          pointToInsert);
  // So, if there is no previous visible node,
  // or, if both nodes of the insertion point is <br> elements,
  // or, if the previous visible node is different block,
  // we need to skip the following <br>.  So, otherwise, we can insert the
  // block at the insertion point.
  if (NS_WARN_IF(backwardScanFromPointToInsertResult.Failed()) ||
      backwardScanFromPointToInsertResult.ReachedInlineEditingHostBoundary() ||
      backwardScanFromPointToInsertResult.ReachedBRElement() ||
      backwardScanFromPointToInsertResult.ReachedCurrentBlockBoundary()) {
    return pointToInsert;
  }

  return forwardScanFromPointToInsertResult
      .template PointAfterReachedContent<EditorDOMPointType>();
}

// static
template <typename EditorDOMPointType, typename EditorDOMPointTypeInput>
EditorDOMPointType HTMLEditUtils::GetBetterCaretPositionToInsertText(
    const EditorDOMPointTypeInput& aPoint) {
  MOZ_ASSERT(aPoint.IsSetAndValid());
  MOZ_ASSERT(HTMLEditUtils::IsSimplyEditableNode(*aPoint.GetContainer()));

  if (aPoint.IsInTextNode()) {
    return aPoint.template To<EditorDOMPointType>();
  }
  if (!aPoint.IsEndOfContainer() && aPoint.GetChild() &&
      aPoint.GetChild()->IsText()) {
    return EditorDOMPointType(aPoint.GetChild(), 0u);
  }
  if (aPoint.IsEndOfContainer()) {
    const WSScanResult previousThing =
        WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary(
            {WSRunScanner::Option::OnlyEditableNodes}, aPoint);
    if (previousThing.InVisibleOrCollapsibleCharacters()) {
      return EditorDOMPointType::AtEndOf(*previousThing.TextPtr());
    }
  }
  if (HTMLEditUtils::CanNodeContain(*aPoint.GetContainer(),
                                    *nsGkAtoms::textTagName)) {
    return aPoint.template To<EditorDOMPointType>();
  }
  if (MOZ_UNLIKELY(aPoint.GetContainer()->IsEditingHost() ||
                   !aPoint.template GetContainerParentAs<nsIContent>() ||
                   !HTMLEditUtils::CanNodeContain(
                       *aPoint.template ContainerParentAs<nsIContent>(),
                       *nsGkAtoms::textTagName))) {
    return EditorDOMPointType();
  }
  return aPoint.ParentPoint().template To<EditorDOMPointType>();
}

// static
template <typename EditorDOMPointType, typename EditorDOMPointTypeInput>
Result<EditorDOMPointType, nsresult>
HTMLEditUtils::ComputePointToPutCaretInElementIfOutside(
    const Element& aElement, const EditorDOMPointTypeInput& aCurrentPoint) {
  MOZ_ASSERT(aCurrentPoint.IsSet());

  // FYI: This was moved from
  // https://searchfox.org/mozilla-central/rev/d3c2f51d89c3ca008ff0cb5a057e77ccd973443e/editor/libeditor/HTMLEditSubActionHandler.cpp#9193

  // Use range boundaries and RangeUtils::CompareNodeToRange() to compare
  // selection start to new block.
  bool nodeBefore, nodeAfter;
  nsresult rv =
      RangeUtils::CompareNodeToRangeBoundaries<TreeKind::ShadowIncludingDOM>(
          const_cast<Element*>(&aElement), aCurrentPoint.ToRawRangeBoundary(),
          aCurrentPoint.ToRawRangeBoundary(), &nodeBefore, &nodeAfter);
  if (NS_FAILED(rv)) {
    NS_WARNING("RangeUtils::CompareNodeToRange() failed");
    return Err(rv);
  }

  if (nodeBefore && nodeAfter) {
    return EditorDOMPointType();  // aCurrentPoint is in aElement
  }

  if (nodeBefore) {
    // selection is after block.  put at end of block.
    const nsIContent* lastEditableContent = HTMLEditUtils::GetLastChild(
        aElement, {LeafNodeOption::IgnoreNonEditableNode},
        BlockInlineCheck::UseComputedDisplayOutsideStyle);
    if (!lastEditableContent) {
      lastEditableContent = &aElement;
    }
    if (lastEditableContent->IsText() ||
        HTMLEditUtils::IsContainerNode(*lastEditableContent)) {
      return EditorDOMPointType::AtEndOf(*lastEditableContent);
    }
    MOZ_ASSERT(lastEditableContent->GetParentNode());
    return EditorDOMPointType::After(*lastEditableContent);
  }

  // selection is before block.  put at start of block.
  const nsIContent* firstEditableContent = HTMLEditUtils::GetFirstChild(
      aElement, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (!firstEditableContent) {
    firstEditableContent = &aElement;
  }
  if (firstEditableContent->IsText() ||
      HTMLEditUtils::IsContainerNode(*firstEditableContent)) {
    MOZ_ASSERT(firstEditableContent->GetParentNode());
    // XXX Shouldn't this be EditorDOMPointType(firstEditableContent, 0u)?
    return EditorDOMPointType(firstEditableContent);
  }
  // XXX And shouldn't this be EditorDOMPointType(firstEditableContent)?
  return EditorDOMPointType(firstEditableContent, 0u);
}

// static
template <typename EditorLineBreakType, typename EditorDOMPointType>
Maybe<EditorLineBreakType>
HTMLEditUtils::GetLineBreakBeforeBlockBoundaryIfPointIsBetweenThem(
    const EditorDOMPointType& aPoint, const Element& aEditingHost) {
  MOZ_ASSERT(aPoint.IsSet());
  if (MOZ_UNLIKELY(!aPoint.IsInContentNode())) {
    return Nothing{};
  }
  const WSScanResult previousThing =
      WSRunScanner::ScanPreviousVisibleNodeOrBlockBoundary({}, aPoint,
                                                           &aEditingHost);
  if (!previousThing.ReachedLineBreak()) {
    return Nothing{};  // No preceding line break.
  }
  const WSScanResult nextThing =
      WSRunScanner::ScanInclusiveNextVisibleNodeOrBlockBoundary({}, aPoint,
                                                                &aEditingHost);
  if (!nextThing.ReachedBlockBoundary()) {
    return Nothing{};  // The line break is not followed by a block boundary so
                       // that it's a visible line break.
  }
  return Some(previousThing.CreateEditorLineBreak<EditorLineBreakType>());
}

// static
bool HTMLEditUtils::IsInlineStyleSetByElement(
    const nsIContent& aContent, const EditorInlineStyle& aStyle,
    const nsAString* aValue, nsAString* aOutValue /* = nullptr */) {
  for (Element* element : aContent.InclusiveAncestorsOfType<Element>()) {
    if (aStyle.mHTMLProperty != element->NodeInfo()->NameAtom()) {
      continue;
    }
    if (!aStyle.mAttribute) {
      return true;
    }
    nsAutoString value;
    element->GetAttr(aStyle.mAttribute, value);
    if (aOutValue) {
      *aOutValue = value;
    }
    if (!value.IsEmpty()) {
      if (!aValue) {
        return true;
      }
      if (aValue->Equals(value, nsCaseInsensitiveStringComparator)) {
        return true;
      }
      // We found the prop with the attribute, but the value doesn't match.
      return false;
    }
  }
  return false;
}

// static
size_t HTMLEditUtils::CollectChildren(
    const nsINode& aNode,
    nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents,
    size_t aIndexToInsertChildren, const CollectChildrenOptions& aOptions) {
  // FYI: This was moved from
  // https://searchfox.org/mozilla-central/rev/4bce7d85ba4796dd03c5dcc7cfe8eee0e4c07b3b/editor/libeditor/HTMLEditSubActionHandler.cpp#6261

  size_t numberOfFoundChildren = 0;
  for (nsIContent* content =
           GetFirstChild(aNode, {LeafNodeOption::IgnoreNonEditableNode},
                         BlockInlineCheck::UseComputedDisplayOutsideStyle);
       content; content = content->GetNextSibling()) {
    if ((aOptions.contains(CollectChildrenOption::CollectListChildren) &&
         (HTMLEditUtils::IsListElement(*content) ||
          HTMLEditUtils::IsListItemElement(*content))) ||
        (aOptions.contains(CollectChildrenOption::CollectTableChildren) &&
         HTMLEditUtils::IsAnyTableElementExceptColumnElement(*content))) {
      numberOfFoundChildren += HTMLEditUtils::CollectChildren(
          *content, aOutArrayOfContents,
          aIndexToInsertChildren + numberOfFoundChildren, aOptions);
      continue;
    }

    if (aOptions.contains(CollectChildrenOption::IgnoreNonEditableChildren) &&
        !EditorUtils::IsEditableContent(*content, EditorType::HTML)) {
      continue;
    }
    if (aOptions.contains(CollectChildrenOption::IgnoreInvisibleTextNodes) &&
        content->IsText() &&
        !HTMLEditUtils::IsVisibleTextNode(*content->AsText(),
                                          TreatInvisibleLineBreakAs::Visible)) {
      continue;
    }
    aOutArrayOfContents.InsertElementAt(
        aIndexToInsertChildren + numberOfFoundChildren++, *content);
  }
  return numberOfFoundChildren;
}

// static
size_t HTMLEditUtils::CollectEmptyInlineContainerDescendants(
    const nsINode& aNode,
    nsTArray<OwningNonNull<nsIContent>>& aOutArrayOfContents,
    const EmptyCheckOptions& aOptions, BlockInlineCheck aBlockInlineCheck) {
  size_t numberOfFoundElements = 0;
  for (Element* element = aNode.GetFirstElementChild(); element;) {
    if (HTMLEditUtils::IsEmptyInlineContainer(
            *element, aOptions,
            UseComputedDisplayOutsideStyleIfAuto(aBlockInlineCheck))) {
      aOutArrayOfContents.AppendElement(*element);
      numberOfFoundElements++;
      nsIContent* nextContent = element->GetNextNonChildNode(&aNode);
      element = nullptr;
      for (; nextContent; nextContent = nextContent->GetNextNode(&aNode)) {
        if (nextContent->IsElement()) {
          element = nextContent->AsElement();
          break;
        }
      }
      continue;
    }

    nsIContent* nextContent = element->GetNextNode(&aNode);
    element = nullptr;
    for (; nextContent; nextContent = nextContent->GetNextNode(&aNode)) {
      if (nextContent->IsElement()) {
        element = nextContent->AsElement();
        break;
      }
    }
  }
  return numberOfFoundElements;
}

// static
bool HTMLEditUtils::ElementHasAttributeExcept(const Element& aElement,
                                              const nsAtom& aAttribute1,
                                              const nsAtom& aAttribute2,
                                              const nsAtom& aAttribute3) {
  // FYI: This was moved from
  // https://searchfox.org/mozilla-central/rev/0b1543e85d13c30a13c57e959ce9815a3f0fa1d3/editor/libeditor/HTMLStyleEditor.cpp#1626
  for (auto i : IntegerRange<uint32_t>(aElement.GetAttrCount())) {
    const nsAttrName* name = aElement.GetAttrNameAt(i);
    if (!name->NamespaceEquals(kNameSpaceID_None)) {
      return true;
    }

    if (name->LocalName() == &aAttribute1 ||
        name->LocalName() == &aAttribute2 ||
        name->LocalName() == &aAttribute3) {
      continue;  // Ignore the given attribute
    }

    // Ignore empty style, class and id attributes because those attributes are
    // not meaningful with empty value.
    if (name->LocalName() == nsGkAtoms::style ||
        name->LocalName() == nsGkAtoms::_class ||
        name->LocalName() == nsGkAtoms::id) {
      if (aElement.HasNonEmptyAttr(name->LocalName())) {
        return true;
      }
      continue;
    }

    // Ignore special _moz attributes
    nsAutoString attrString;
    name->LocalName()->ToString(attrString);
    if (!StringBeginsWith(attrString, u"_moz"_ns)) {
      return true;
    }
  }
  // if we made it through all of them without finding a real attribute
  // other than aAttribute, then return true
  return false;
}

bool HTMLEditUtils::GetNormalizedHTMLColorValue(const nsAString& aColorValue,
                                                nsAString& aNormalizedValue) {
  nsAttrValue value;
  if (!value.ParseColor(aColorValue)) {
    aNormalizedValue = aColorValue;
    return false;
  }
  nscolor color = NS_RGB(0, 0, 0);
  MOZ_ALWAYS_TRUE(value.GetColorValue(color));
  aNormalizedValue = NS_ConvertASCIItoUTF16(nsPrintfCString(
      "#%02x%02x%02x", NS_GET_R(color), NS_GET_G(color), NS_GET_B(color)));
  return true;
}

bool HTMLEditUtils::IsSameHTMLColorValue(
    const nsAString& aColorA, const nsAString& aColorB,
    TransparentKeyword aTransparentKeyword) {
  if (aTransparentKeyword == TransparentKeyword::Allowed) {
    const bool isATransparent = aColorA.LowerCaseEqualsLiteral("transparent");
    const bool isBTransparent = aColorB.LowerCaseEqualsLiteral("transparent");
    if (isATransparent || isBTransparent) {
      return isATransparent && isBTransparent;
    }
  }
  nsAttrValue valueA, valueB;
  if (!valueA.ParseColor(aColorA) || !valueB.ParseColor(aColorB)) {
    return false;
  }
  nscolor colorA = NS_RGB(0, 0, 0), colorB = NS_RGB(0, 0, 0);
  MOZ_ALWAYS_TRUE(valueA.GetColorValue(colorA));
  MOZ_ALWAYS_TRUE(valueB.GetColorValue(colorB));
  return colorA == colorB;
}

bool HTMLEditUtils::MaybeCSSSpecificColorValue(const nsAString& aColorValue) {
  if (aColorValue.IsEmpty() || aColorValue.First() == '#') {
    return false;  // Quick return for the most cases.
  }

  nsAutoString colorValue(aColorValue);
  colorValue.CompressWhitespace(true, true);
  if (colorValue.LowerCaseEqualsASCII("transparent")) {
    return true;
  }
  if (colorValue.IsEmpty() || colorValue.First() == '#') {
    return false;
  }
  const NS_ConvertUTF16toUTF8 colorU8(colorValue);
  StyleAbsoluteColor color;
  if (Servo_ColorNameToRgb(&colorU8, &color)) {
    return false;
  }
  if (colorValue.LowerCaseEqualsASCII("initial") ||
      colorValue.LowerCaseEqualsASCII("inherit") ||
      colorValue.LowerCaseEqualsASCII("unset") ||
      colorValue.LowerCaseEqualsASCII("revert") ||
      colorValue.LowerCaseEqualsASCII("currentcolor")) {
    return true;
  }
  return ServoCSSParser::IsValidCSSColor(colorU8);
}

static bool ComputeColor(const nsAString& aColorValue, nscolor* aColor,
                         bool* aIsCurrentColor) {
  return ServoCSSParser::ComputeColor(
      nullptr, NS_ConvertUTF16toUTF8(aColorValue), aColor, aIsCurrentColor);
}

static bool ComputeColor(const nsACString& aColorValue, nscolor* aColor,
                         bool* aIsCurrentColor) {
  return ServoCSSParser::ComputeColor(nullptr, aColorValue, aColor,
                                      aIsCurrentColor);
}

bool HTMLEditUtils::CanConvertToHTMLColorValue(const nsAString& aColorValue) {
  bool isCurrentColor = false;
  nscolor color = NS_RGB(0, 0, 0);
  return ComputeColor(aColorValue, &color, &isCurrentColor) &&
         !isCurrentColor && NS_GET_A(color) == 0xFF;
}

bool HTMLEditUtils::ConvertToNormalizedHTMLColorValue(
    const nsAString& aColorValue, nsAString& aNormalizedValue) {
  bool isCurrentColor = false;
  nscolor color = NS_RGB(0, 0, 0);
  if (!ComputeColor(aColorValue, &color, &isCurrentColor) || isCurrentColor ||
      NS_GET_A(color) != 0xFF) {
    aNormalizedValue = aColorValue;
    return false;
  }
  aNormalizedValue.Truncate();
  aNormalizedValue.AppendPrintf("#%02x%02x%02x", NS_GET_R(color),
                                NS_GET_G(color), NS_GET_B(color));
  return true;
}

bool HTMLEditUtils::GetNormalizedCSSColorValue(const nsAString& aColorValue,
                                               ZeroAlphaColor aZeroAlphaColor,
                                               nsAString& aNormalizedValue) {
  bool isCurrentColor = false;
  nscolor color = NS_RGB(0, 0, 0);
  if (!ComputeColor(aColorValue, &color, &isCurrentColor)) {
    aNormalizedValue = aColorValue;
    return false;
  }

  // If it's currentcolor, let's return it as-is since we cannot resolve it
  // without ancestors.
  if (isCurrentColor) {
    aNormalizedValue = aColorValue;
    return true;
  }

  if (aZeroAlphaColor == ZeroAlphaColor::TransparentKeyword &&
      NS_GET_A(color) == 0) {
    aNormalizedValue.AssignLiteral("transparent");
    return true;
  }

  // Get serialized color value (i.e., "rgb()" or "rgba()").
  aNormalizedValue.Truncate();
  nsStyleUtil::GetSerializedColorValue(color, aNormalizedValue);
  return true;
}

template <typename CharType>
bool HTMLEditUtils::IsSameCSSColorValue(const nsTSubstring<CharType>& aColorA,
                                        const nsTSubstring<CharType>& aColorB) {
  bool isACurrentColor = false;
  nscolor colorA = NS_RGB(0, 0, 0);
  if (!ComputeColor(aColorA, &colorA, &isACurrentColor)) {
    return false;
  }
  bool isBCurrentColor = false;
  nscolor colorB = NS_RGB(0, 0, 0);
  if (!ComputeColor(aColorB, &colorB, &isBCurrentColor)) {
    return false;
  }
  if (isACurrentColor || isBCurrentColor) {
    return isACurrentColor && isBCurrentColor;
  }
  return colorA == colorB;
}

bool HTMLEditUtils::IsTransparentCSSColor(const nsAString& aColor) {
  nsAutoString normalizedCSSColorValue;
  return GetNormalizedCSSColorValue(aColor, ZeroAlphaColor::TransparentKeyword,
                                    normalizedCSSColorValue) &&
         normalizedCSSColorValue.EqualsASCII("transparent");
}

/******************************************************************************
 * operator<<() for enum classes of HTMLEditUtils
 ******************************************************************************/

std::ostream& operator<<(std::ostream& aStream,
                         const HTMLEditUtils::AncestorType& aType) {
  constexpr static const char* names[] = {
      "ClosestBlockElement",
      "ClosestContainerElement",
      "MostDistantInlineElementInBlock",
      "IgnoreHRElement",
      "ClosestButtonElement",
      "StopAtClosestButtonElement",
      "ReturnAncestorLimiterIfNoProperAncestor",
      "EditableElement",
  };
  MOZ_ASSERT(static_cast<uint32_t>(aType) < std::size(names));
  return aStream << names[static_cast<uint32_t>(aType)];
}

std::ostream& operator<<(std::ostream& aStream,
                         const HTMLEditUtils::AncestorTypes& aTypes) {
  aStream << "{";
  bool first = true;
  for (const auto t : aTypes) {
    if (!first) {
      aStream << ", ";
    }
    aStream << ToString(t).c_str();
    first = false;
  }
  return aStream << "}";
}

std::ostream& operator<<(std::ostream& aStream,
                         const HTMLEditUtils::EditablePointOption& aOption) {
  constexpr static const char* names[] = {
      "RecognizeInvisibleWhiteSpaces",
      "StopAtComment",
      "StopAtListElement",
      "StopAtListItemElement",
      "StopAtTableElement",
      "StopAtAnyTableElement",
  };
  MOZ_ASSERT(static_cast<uint32_t>(aOption) < std::size(names));
  return aStream << names[static_cast<uint32_t>(aOption)];
}

std::ostream& operator<<(std::ostream& aStream,
                         const HTMLEditUtils::EditablePointOptions& aOptions) {
  aStream << "{";
  bool first = true;
  for (const auto option : aOptions) {
    if (!first) {
      aStream << ", ";
    }
    aStream << ToString(option).c_str();
    first = false;
  }
  return aStream << "}";
}

std::ostream& operator<<(std::ostream& aStream,
                         const HTMLEditUtils::EmptyCheckOption& aOption) {
  constexpr static const char* names[] = {
      "TreatSingleBRElementAsVisible",
      "TreatBlockAsVisible",
      "TreatListItemAsVisible",
      "TreatTableCellAsVisible",
      "TreatNonEditableContentAsInvisible",
      "TreatCommentAsVisible",
      "SafeToAskLayout",
  };
  MOZ_ASSERT(static_cast<uint32_t>(aOption) < std::size(names));
  return aStream << names[static_cast<uint32_t>(aOption)];
}

std::ostream& operator<<(std::ostream& aStream,
                         const HTMLEditUtils::EmptyCheckOptions& aOptions) {
  aStream << "{";
  bool first = true;
  for (const auto t : aOptions) {
    if (!first) {
      aStream << ", ";
    }
    aStream << ToString(t).c_str();
    first = false;
  }
  return aStream << "}";
}

std::ostream& operator<<(std::ostream& aStream,
                         const HTMLEditUtils::LeafNodeOption& aOption) {
  constexpr static const char* names[] = {
      "TreatChildBlockAsLeafNode",
      "TreatNonEditableNodeAsLeafNode",
      "IgnoreNonEditableNode",
      "TreatCommentAsLeafNode",
      "IgnoreEmptyText",
      "IgnoreInvisibleText",
      "IgnoreInvisibleInlineVoidElements",
      "IgnoreAnyEmptyInlineContainers",
      "IgnoreInvisibleEmptyInlineContainers",
  };
  MOZ_ASSERT(static_cast<uint32_t>(aOption) < std::size(names));
  return aStream << names[static_cast<uint32_t>(aOption)];
}

std::ostream& operator<<(std::ostream& aStream,
                         const HTMLEditUtils::LeafNodeOptions& aOptions) {
  aStream << "{";
  bool first = true;
  for (const auto t : aOptions) {
    if (!first) {
      aStream << ", ";
    }
    aStream << ToString(t).c_str();
    first = false;
  }
  return aStream << "}";
}

/******************************************************************************
 * SelectedTableCellScanner
 ******************************************************************************/

SelectedTableCellScanner::SelectedTableCellScanner(
    const AutoClonedRangeArray& aRanges) {
  if (aRanges.Ranges().IsEmpty()) {
    return;
  }
  Element* firstSelectedCellElement =
      HTMLEditUtils::GetTableCellElementIfOnlyOneSelected(
          aRanges.FirstRangeRef());
  if (!firstSelectedCellElement) {
    return;  // We're not in table cell selection mode.
  }
  mSelectedCellElements.SetCapacity(aRanges.Ranges().Length());
  mSelectedCellElements.AppendElement(*firstSelectedCellElement);
  for (uint32_t i = 1; i < aRanges.Ranges().Length(); i++) {
    dom::Range* range = aRanges.Ranges()[i];
    if (NS_WARN_IF(!range) || NS_WARN_IF(!range->IsPositioned())) {
      continue;  // Shouldn't occur in normal conditions.
    }
    // Just ignore selection ranges which do not select only one table
    // cell element.  This is possible case if web apps sets multiple
    // selections and first range selects a table cell element.
    if (Element* selectedCellElement =
            HTMLEditUtils::GetTableCellElementIfOnlyOneSelected(*range)) {
      mSelectedCellElements.AppendElement(*selectedCellElement);
    }
  }
}

}  // namespace mozilla
