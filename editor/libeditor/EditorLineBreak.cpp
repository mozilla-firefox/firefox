/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EditorLineBreak.h"

#include "HTMLEditUtils.h"
#include "WSRunScanner.h"
#include "mozilla/dom/AncestorIterator.h"

namespace mozilla {

using namespace dom;

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool, IsFollowedByBlockBoundary,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsFollowedByBlockBoundary(
    const dom::Element* aAncestorLimiter /* = nullptr */) const {
  return IsHTMLBRElement()
             ? HTMLEditUtils::IsBRElementFollowedByBlockBoundary(
                   BRElementRef(), aAncestorLimiter)
             : HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
                   To<EditorRawDOMPoint>(),
                   HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes,
                   aAncestorLimiter);
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool,
                                              IsFollowedByCurrentBlockBoundary,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsFollowedByCurrentBlockBoundary(
    const dom::Element* aAncestorLimiter /* = nullptr */) const {
  return IsHTMLBRElement()
             ? HTMLEditUtils::IsBRElementFollowedByCurrentBlockBoundary(
                   BRElementRef(), aAncestorLimiter)
             : HTMLEditUtils::
                   IsPreformattedLineBreakFollowedByCurrentBlockBoundary(
                       To<EditorRawDOMPoint>(),
                       HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes,
                       aAncestorLimiter);
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool,
                                              IsFollowingCurrentBlockBoundary,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsFollowingCurrentBlockBoundary(
    const dom::Element* aAncestorLimiter /* = nullptr */) const {
  return IsHTMLBRElement()
             ? HTMLEditUtils::IsBRElementFollowingCurrentBlockBoundary(
                   BRElementRef(), aAncestorLimiter)
             : HTMLEditUtils::
                   IsPreformattedLineBreakFollowingCurrentBlockBoundary(
                       To<EditorRawDOMPoint>(),
                       HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes,
                       aAncestorLimiter);
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool, IsFollowedByLineBoundary,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsFollowedByLineBoundary(
    const dom::Element* aAncestorLimiter /* = nullptr */) const {
  return IsHTMLBRElement()
             ? HTMLEditUtils::IsBRElementFollowedByLineBoundary(
                   BRElementRef(), aAncestorLimiter)
             : HTMLEditUtils::IsPreformattedLineBreakFollowedByLineBoundary(
                   To<EditorRawDOMPoint>(),
                   HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes,
                   aAncestorLimiter);
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool, IsFollowingLineBoundary,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsFollowingLineBoundary(
    const dom::Element* aAncestorLimiter /* = nullptr */) const {
  return IsHTMLBRElement()
             ? HTMLEditUtils::IsBRElementFollowingLineBoundary(BRElementRef(),
                                                               aAncestorLimiter)
             : HTMLEditUtils::IsPreformattedLineBreakFollowingLineBoundary(
                   To<EditorRawDOMPoint>(),
                   HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes,
                   aAncestorLimiter);
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool, IsFollowingAnotherLineBreak,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsFollowingAnotherLineBreak(
    const dom::Element* aAncestorLimiter /* = nullptr */) const {
  return IsHTMLBRElement()
             ? HTMLEditUtils::IsBRElementFollowingLineBreak(BRElementRef(),
                                                            aAncestorLimiter)
             : HTMLEditUtils::IsPreformattedLineBreakFollowingLineBreak(
                   To<EditorRawDOMPoint>(),
                   HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes,
                   aAncestorLimiter);
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool, IsPaddingForEmptyBlock,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsPaddingForEmptyBlock(
    const dom::Element* /*aAncestorLimiter = nullptr */) const {
  return IsFollowedByCurrentBlockBoundary() &&
         IsFollowingCurrentBlockBoundary();
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool, IsUnnecessary,
                                              TreatAsUnnecessaryLineBreak,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsUnnecessary(
    TreatAsUnnecessaryLineBreak aTreatAsUnnecessaryLineBreak,
    const dom::Element* aAncestorLimiter /* = nullptr */) const {
  return IsHTMLBRElement()
             ? HTMLEditUtils::IsUnnecessaryBRElement(
                   BRElementRef(), aTreatAsUnnecessaryLineBreak,
                   aAncestorLimiter)
             : HTMLEditUtils::IsUnnecessaryPreformattedLineBreak(
                   To<EditorRawDOMPoint>(), aTreatAsUnnecessaryLineBreak,
                   HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes,
                   aAncestorLimiter);
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(bool, IsSignificant,
                                              TreatAsUnnecessaryLineBreak,
                                              const Element*);

template <typename ContentType>
bool EditorLineBreakBase<ContentType>::IsSignificant(
    TreatAsUnnecessaryLineBreak aTreatAsUnnecessaryLineBreak,
    const dom::Element* aAncestorLimiter /* = nullptr */) const {
  return IsHTMLBRElement()
             ? HTMLEditUtils::IsSignificantBRElement(
                   BRElementRef(), aTreatAsUnnecessaryLineBreak,
                   aAncestorLimiter)
             : HTMLEditUtils::IsSignificantPreformattedLineBreak(
                   To<EditorRawDOMPoint>(), aTreatAsUnnecessaryLineBreak,
                   HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes,
                   aAncestorLimiter);
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(
    Element*, GetBlockElementIfFollowedByBlockBoundary, const Element*);

template <typename ContentType>
Element*
EditorLineBreakBase<ContentType>::GetBlockElementIfFollowedByBlockBoundary(
    const Element* aAncestorLimiter /* = nullptr */) const {
  Element* blockElement = nullptr;
  IsHTMLBRElement()
      ? (void)HTMLEditUtils::IsBRElementFollowedByBlockBoundary(
            BRElementRef(), aAncestorLimiter, &blockElement)
      : (void)HTMLEditUtils::IsPreformattedLineBreakFollowedByBlockBoundary(
            To<EditorRawDOMPoint>(),
            HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes, aAncestorLimiter,
            &blockElement);
  return blockElement;
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(
    Element*, GetBlockElementIfFollowedByCurrentBlockBoundary, const Element*);

template <typename ContentType>
Element* EditorLineBreakBase<ContentType>::
    GetBlockElementIfFollowedByCurrentBlockBoundary(
        const Element* aAncestorLimiter /* = nullptr */) const {
  Element* blockElement = nullptr;
  IsHTMLBRElement()
      ? (void)HTMLEditUtils::IsBRElementFollowedByCurrentBlockBoundary(
            BRElementRef(), aAncestorLimiter, &blockElement)
      : (void)HTMLEditUtils::
            IsPreformattedLineBreakFollowedByCurrentBlockBoundary(
                To<EditorRawDOMPoint>(),
                HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes, aAncestorLimiter,
                &blockElement);
  return blockElement;
}

NS_INSTANTIATE_EDITOR_LINE_BREAK_CONST_METHOD(
    Element*, GetBlockElementIfFollowedByOtherBlockBoundary, const Element*);

template <typename ContentType>
Element*
EditorLineBreakBase<ContentType>::GetBlockElementIfFollowedByOtherBlockBoundary(
    const Element* aAncestorLimiter /* = nullptr */) const {
  Element* blockElement = nullptr;
  IsHTMLBRElement()
      ? (void)HTMLEditUtils::IsBRElementFollowedByOtherBlockBoundary(
            BRElementRef(), aAncestorLimiter, &blockElement)
      : (void)
            HTMLEditUtils::IsPreformattedLineBreakFollowedByOtherBlockBoundary(
                To<EditorRawDOMPoint>(),
                HTMLEditUtils::SkipWhiteSpaceStyleCheck::Yes, aAncestorLimiter,
                &blockElement);
  return blockElement;
}

}  // namespace mozilla
