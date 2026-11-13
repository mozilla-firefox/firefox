/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ErrorList.h"
#include "HTMLEditor.h"
#include "HTMLEditorInlines.h"
#include "HTMLEditorNestedClasses.h"

#include "AutoClonedRangeArray.h"
#include "CSSEditUtils.h"
#include "EditAction.h"
#include "EditorLineBreak.h"
#include "EditorUtils.h"
#include "HTMLEditHelpers.h"
#include "HTMLEditUtils.h"
#include "PendingStyles.h"
#include "SelectionState.h"
#include "WSRunScanner.h"

#include "mozilla/Assertions.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/EditorForwards.h"
#include "mozilla/mozalloc.h"
#include "mozilla/SelectionState.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/HTMLBRElement.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/Text.h"

#include "nsAString.h"
#include "nsAtom.h"
#include "nsAttrName.h"
#include "nsAttrValue.h"
#include "nsCaseTreatment.h"
#include "nsColor.h"
#include "nsComponentManagerUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "nsIContent.h"
#include "nsINode.h"
#include "nsIPrincipal.h"
#include "nsISupportsImpl.h"
#include "nsLiteralString.h"
#include "nsNameSpaceManager.h"
#include "nsReadableUtils.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsStyledElement.h"
#include "nsTArray.h"
#include "nsTextNode.h"
#include "nsUnicharUtils.h"

// from vs/Windows Kits/10/.../rpcndr.h
#ifdef small
#  undef small
#endif

namespace mozilla {

using namespace dom;

using EditablePointOption = HTMLEditUtils::EditablePointOption;
using EditablePointOptions = HTMLEditUtils::EditablePointOptions;
using EmptyCheckOption = HTMLEditUtils::EmptyCheckOption;
using LeafNodeOption = HTMLEditUtils::LeafNodeOption;
using TreatInvisibleLineBreakAs = HTMLEditUtils::TreatInvisibleLineBreakAs;

template nsresult HTMLEditor::SetInlinePropertiesAsSubAction(
    const AutoTArray<EditorInlineStyleAndValue, 1>& aStylesToSet,
    const Element& aEditingHost);
template nsresult HTMLEditor::SetInlinePropertiesAsSubAction(
    const AutoTArray<EditorInlineStyleAndValue, 32>& aStylesToSet,
    const Element& aEditingHost);

template nsresult HTMLEditor::SetInlinePropertiesAroundRanges(
    AutoClonedRangeArray&, const AutoTArray<EditorInlineStyleAndValue, 1>&,
    const Element&);
template nsresult HTMLEditor::SetInlinePropertiesAroundRanges(
    AutoClonedRangeArray&, const AutoTArray<EditorInlineStyleAndValue, 32>&,
    const Element&);

nsresult HTMLEditor::SetInlinePropertyAsAction(nsStaticAtom& aProperty,
                                               nsStaticAtom* aAttribute,
                                               const nsAString& aValue,
                                               nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(
      *this,
      HTMLEditUtils::GetEditActionForFormatText(aProperty, aAttribute, true),
      aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }
  if (!IsStyleEditable(editingHost)) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  switch (editActionData.GetEditAction()) {
    case EditAction::eSetFontFamilyProperty:
      MOZ_ASSERT(!aValue.IsVoid());
      // XXX Should we trim unnecessary white-spaces?
      editActionData.SetData(aValue);
      break;
    case EditAction::eSetColorProperty:
    case EditAction::eSetBackgroundColorPropertyInline:
      editActionData.SetColorData(aValue);
      break;
    default:
      break;
  }

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  // XXX Due to bug 1659276 and bug 1659924, we should not scroll selection
  //     into view after setting the new style.
  AutoPlaceholderBatch treatAsOneTransaction(*this, ScrollSelectionIntoView::No,
                                             __FUNCTION__);

  nsStaticAtom* property = &aProperty;
  nsStaticAtom* attribute = aAttribute;
  nsString value(aValue);
  if (attribute == nsGkAtoms::color || attribute == nsGkAtoms::bgcolor) {
    if (!IsCSSEnabled()) {
      // We allow CSS style color value even in the HTML mode.  In the cases,
      // we will apply the style with CSS.  For considering it in the value
      // as-is if it's a known CSS keyboard, `rgb()` or `rgba()` style.
      // NOTE: It may be later that we set the color into the DOM tree and at
      // that time, IsCSSEnabled() may return true.  E.g., setting color value
      // to collapsed selection, then, change the CSS enabled, finally, user
      // types text there.
      if (!HTMLEditUtils::MaybeCSSSpecificColorValue(value)) {
        HTMLEditUtils::GetNormalizedHTMLColorValue(value, value);
      }
    } else {
      HTMLEditUtils::GetNormalizedCSSColorValue(
          value, HTMLEditUtils::ZeroAlphaColor::RGBAValue, value);
    }
  }

  AutoTArray<EditorInlineStyle, 1> stylesToRemove;
  if (&aProperty == nsGkAtoms::sup) {
    // Superscript and Subscript styles are mutually exclusive.
    stylesToRemove.AppendElement(EditorInlineStyle(*nsGkAtoms::sub));
  } else if (&aProperty == nsGkAtoms::sub) {
    // Superscript and Subscript styles are mutually exclusive.
    stylesToRemove.AppendElement(EditorInlineStyle(*nsGkAtoms::sup));
  }
  // Handling `<tt>` element code was implemented for composer (bug 115922).
  // This shouldn't work with `Document.execCommand()`.  Currently, aPrincipal
  // is set only when the root caller is Document::ExecCommand() so that
  // we should handle `<tt>` element only when aPrincipal is nullptr that
  // must be only when XUL command is executed on composer.
  else if (!aPrincipal) {
    if (&aProperty == nsGkAtoms::tt) {
      stylesToRemove.AppendElement(
          EditorInlineStyle(*nsGkAtoms::font, nsGkAtoms::face));
    } else if (&aProperty == nsGkAtoms::font && aAttribute == nsGkAtoms::face) {
      if (!value.LowerCaseEqualsASCII("tt")) {
        stylesToRemove.AppendElement(EditorInlineStyle(*nsGkAtoms::tt));
      } else {
        stylesToRemove.AppendElement(
            EditorInlineStyle(*nsGkAtoms::font, nsGkAtoms::face));
        // Override property, attribute and value if the new font face value is
        // "tt".
        property = nsGkAtoms::tt;
        attribute = nullptr;
        value.Truncate();
      }
    }
  }

  if (!stylesToRemove.IsEmpty()) {
    nsresult rv =
        RemoveInlinePropertiesAsSubAction(stylesToRemove, *editingHost);
    if (NS_FAILED(rv)) {
      NS_WARNING("HTMLEditor::RemoveInlinePropertiesAsSubAction() failed");
      return rv;
    }
  }

  AutoTArray<EditorInlineStyleAndValue, 1> styleToSet;
  styleToSet.AppendElement(
      attribute
          ? EditorInlineStyleAndValue(*property, *attribute, std::move(value))
          : EditorInlineStyleAndValue(*property));
  rv = SetInlinePropertiesAsSubAction(styleToSet, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetInlinePropertiesAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::SetInlineProperty(const nsAString& aProperty,
                                            const nsAString& aAttribute,
                                            const nsAString& aValue) {
  nsStaticAtom* property = NS_GetStaticAtom(aProperty);
  if (NS_WARN_IF(!property)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsStaticAtom* attribute = EditorUtils::GetAttributeAtom(aAttribute);
  AutoEditActionDataSetter editActionData(
      *this,
      HTMLEditUtils::GetEditActionForFormatText(*property, attribute, true));
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (NS_WARN_IF(!editingHost)) {
    return NS_ERROR_FAILURE;
  }
  if (!IsStyleEditable(editingHost)) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  switch (editActionData.GetEditAction()) {
    case EditAction::eSetFontFamilyProperty:
      MOZ_ASSERT(!aValue.IsVoid());
      // XXX Should we trim unnecessary white-spaces?
      editActionData.SetData(aValue);
      break;
    case EditAction::eSetColorProperty:
    case EditAction::eSetBackgroundColorPropertyInline:
      editActionData.SetColorData(aValue);
      break;
    default:
      break;
  }
  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoTArray<EditorInlineStyleAndValue, 1> styleToSet;
  styleToSet.AppendElement(
      attribute ? EditorInlineStyleAndValue(*property, *attribute, aValue)
                : EditorInlineStyleAndValue(*property));
  rv = SetInlinePropertiesAsSubAction(styleToSet, *editingHost);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::SetInlinePropertiesAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

template <size_t N>
nsresult HTMLEditor::SetInlinePropertiesAsSubAction(
    const AutoTArray<EditorInlineStyleAndValue, N>& aStylesToSet,
    const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!aStylesToSet.IsEmpty());

  DebugOnly<nsresult> rvIgnored = CommitComposition();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::CommitComposition() failed, but ignored");
  if (MOZ_UNLIKELY(&aEditingHost !=
                   ComputeEditingHost(LimitInBodyElement::No))) {
    NS_WARNING("Editing host has been changed during committing composition");
    return NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE;
  }

  if (SelectionRef().IsCollapsed()) {
    // Manipulating text attributes on a collapsed selection only sets state
    // for the next text insertion
    mPendingStylesToApplyToNewContent->PreserveStyles(aStylesToSet);
    return NS_OK;
  }

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result.unwrapErr();
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertElement, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // TODO: We don't need AutoTransactionsConserveSelection here in the normal
  //       cases, but removing this may cause the behavior with the legacy
  //       mutation event listeners.  We should try to delete this in a bug.
  AutoTransactionsConserveSelection dontChangeMySelection(*this);

  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());
  nsresult rv = SetInlinePropertiesAroundRanges(selectionRanges, aStylesToSet,
                                                aEditingHost);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::SetInlinePropertiesAroundRanges() failed");
    return rv;
  }
  MOZ_ASSERT(!selectionRanges.HasSavedRanges());
  rv = selectionRanges.ApplyTo(SelectionRef());
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoClonedSelectionRangeArray::ApplyTo() failed");
  return rv;
}

template <size_t N>
nsresult HTMLEditor::SetInlinePropertiesAroundRanges(
    AutoClonedRangeArray& aRanges,
    const AutoTArray<EditorInlineStyleAndValue, N>& aStylesToSet,
    const Element& aEditingHost) {
  MOZ_ASSERT(!aRanges.HasSavedRanges());
  for (const EditorInlineStyleAndValue& styleToSet : aStylesToSet) {
    AutoInlineStyleSetter inlineStyleSetter(styleToSet);
    for (OwningNonNull<dom::Range>& domRange : aRanges.Ranges()) {
      inlineStyleSetter.Reset();
      auto rangeOrError =
          [&]() MOZ_CAN_RUN_SCRIPT -> Result<EditorDOMRange, nsresult> {
        EditorDOMRange range(domRange);
        // If we're setting <font>, we want to remove ancestors which set
        // `font-size` or <font size="..."> recursively.  Therefore, for
        // extending the ranges to contain all ancestors in the range, we need
        // to split ancestors first.
        // XXX: Blink and WebKit inserts <font> elements to inner most
        // elements, however, we cannot do it under current design because
        // once we contain ancestors which have `font-size` or are
        // <font size="...">, we lost the original ranges which we wanted to
        // apply the style.  For fixing this, we need to manage both ranges, but
        // it's too expensive especially we allow to run script when we touch
        // the DOM tree.  Additionally, font-size value affects the height
        // of the element, but does not affect the height of ancestor inline
        // elements.  Therefore, following the behavior may cause similar issue
        // as bug 1808906.  So at least for now, we should not do this big work.
        if (styleToSet.IsStyleOfFontElement()) {
          Result<SplitRangeOffResult, nsresult> splitAncestorsResult =
              SplitAncestorStyledInlineElementsAtRangeEdges(
                  range, styleToSet, SplitAtEdges::eDoNotCreateEmptyContainer);
          if (MOZ_UNLIKELY(splitAncestorsResult.isErr())) {
            NS_WARNING(
                "HTMLEditor::SplitAncestorStyledInlineElementsAtRangeEdges() "
                "failed");
            return splitAncestorsResult.propagateErr();
          }
          SplitRangeOffResult unwrappedResult = splitAncestorsResult.unwrap();
          unwrappedResult.IgnoreCaretPointSuggestion();
          range = unwrappedResult.RangeRef();
          if (NS_WARN_IF(!range.IsPositionedAndValid())) {
            return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
          }
        }
        Result<EditorRawDOMRange, nsresult> rangeOrError =
            inlineStyleSetter.ExtendOrShrinkRangeToApplyTheStyle(*this, range,
                                                                 aEditingHost);
        if (MOZ_UNLIKELY(rangeOrError.isErr())) {
          NS_WARNING(
              "HTMLEditor::ExtendOrShrinkRangeToApplyTheStyle() failed, but "
              "ignored");
          return EditorDOMRange();
        }
        return EditorDOMRange(rangeOrError.unwrap());
      }();
      if (MOZ_UNLIKELY(rangeOrError.isErr())) {
        return rangeOrError.unwrapErr();
      }

      const EditorDOMRange range = rangeOrError.unwrap();
      if (!range.IsPositioned()) {
        continue;
      }

      // If the range is collapsed, we should insert new element there.
      if (range.Collapsed()) {
        Result<RefPtr<Text>, nsresult> emptyTextNodeOrError =
            AutoInlineStyleSetter::GetEmptyTextNodeToApplyNewStyle(
                *this, range.StartRef());
        if (MOZ_UNLIKELY(emptyTextNodeOrError.isErr())) {
          NS_WARNING(
              "AutoInlineStyleSetter::GetEmptyTextNodeToApplyNewStyle() "
              "failed");
          return emptyTextNodeOrError.unwrapErr();
        }
        if (MOZ_UNLIKELY(!emptyTextNodeOrError.inspect())) {
          continue;  // Couldn't insert text node there
        }
        RefPtr<Text> emptyTextNode = emptyTextNodeOrError.unwrap();
        Result<CaretPoint, nsresult> caretPointOrError =
            inlineStyleSetter
                .ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle(
                    *this, *emptyTextNode);
        if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
          NS_WARNING(
              "AutoInlineStyleSetter::"
              "ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle() failed");
          return caretPointOrError.unwrapErr();
        }
        DebugOnly<nsresult> rvIgnored = domRange->CollapseTo(emptyTextNode, 0);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                             "Range::CollapseTo() failed, but ignored");
        continue;
      }

      // Use const_cast hack here for preventing the others to update the range.
      AutoTrackDOMRange trackRange(RangeUpdaterRef(),
                                   const_cast<EditorDOMRange*>(&range));
      auto UpdateSelectionRange = [&]() MOZ_CAN_RUN_SCRIPT {
        // If inlineStyleSetter creates elements or setting styles, we should
        // select between start of first element and end of last element.
        if (inlineStyleSetter.FirstHandledPointRef().IsInContentNode()) {
          MOZ_ASSERT(inlineStyleSetter.LastHandledPointRef().IsInContentNode());
          const auto startPoint =
              !inlineStyleSetter.FirstHandledPointRef().IsStartOfContainer()
                  ? inlineStyleSetter.FirstHandledPointRef()
                        .To<EditorRawDOMPoint>()
                  : HTMLEditUtils::GetDeepestEditableStartPointOf<
                        EditorRawDOMPoint>(
                        *inlineStyleSetter.FirstHandledPointRef()
                             .ContainerAs<nsIContent>(),
                        {EditablePointOption::RecognizeInvisibleWhiteSpaces,
                         EditablePointOption::StopAtComment});
          const auto endPoint =
              !inlineStyleSetter.LastHandledPointRef().IsEndOfContainer()
                  ? inlineStyleSetter.LastHandledPointRef()
                        .To<EditorRawDOMPoint>()
                  : HTMLEditUtils::GetDeepestEditableEndPointOf<
                        EditorRawDOMPoint>(
                        *inlineStyleSetter.LastHandledPointRef()
                             .ContainerAs<nsIContent>(),
                        {EditablePointOption::RecognizeInvisibleWhiteSpaces,
                         EditablePointOption::StopAtComment});
          nsresult rv = domRange->SetStartAndEnd(
              startPoint.ToRawRangeBoundary(), endPoint.ToRawRangeBoundary());
          if (NS_SUCCEEDED(rv)) {
            trackRange.StopTracking();
            return;
          }
        }
        // Otherwise, use the range computed with the tracking original range.
        trackRange.Flush(StopTracking::Yes);
        domRange->SetStartAndEnd(range.StartRef().ToRawRangeBoundary(),
                                 range.EndRef().ToRawRangeBoundary());
      };

      // If range is in a text node, apply new style simply.
      if (range.InSameContainer() && range.StartRef().IsInTextNode()) {
        // MOZ_KnownLive(...ContainerAs<Text>()) because of grabbed by `range`.
        // MOZ_KnownLive(styleToSet.*) due to bug 1622253.
        Result<SplitRangeOffFromNodeResult, nsresult>
            wrapTextInStyledElementResult =
                inlineStyleSetter.SplitTextNodeAndApplyStyleToMiddleNode(
                    *this, MOZ_KnownLive(*range.StartRef().ContainerAs<Text>()),
                    range.StartRef().Offset(), range.EndRef().Offset());
        if (MOZ_UNLIKELY(wrapTextInStyledElementResult.isErr())) {
          NS_WARNING("HTMLEditor::SetInlinePropertyOnTextNode() failed");
          return wrapTextInStyledElementResult.unwrapErr();
        }
        // The caller should handle the ranges as Selection if necessary, and we
        // don't want to update aRanges with this result.
        wrapTextInStyledElementResult.inspect().IgnoreCaretPointSuggestion();
        UpdateSelectionRange();
        continue;
      }

      // Collect editable nodes which are entirely contained in the range.
      AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContentsAroundRange;
      {
        ContentSubtreeIterator subtreeIter;
        // If there is no node which is entirely in the range,
        // `ContentSubtreeIterator::Init()` fails, but this is possible case,
        // don't warn it.
        if (NS_SUCCEEDED(
                subtreeIter.Init(range.StartRef().ToRawRangeBoundary(),
                                 range.EndRef().ToRawRangeBoundary()))) {
          for (; !subtreeIter.IsDone(); subtreeIter.Next()) {
            nsINode* node = subtreeIter.GetCurrentNode();
            if (NS_WARN_IF(!node)) {
              return NS_ERROR_FAILURE;
            }
            if (MOZ_UNLIKELY(!node->IsContent())) {
              continue;
            }
            // We don't need to wrap non-editable node in new inline element
            // nor shouldn't modify `style` attribute of non-editable element.
            if (!EditorUtils::IsEditableContent(*node->AsContent(),
                                                EditorType::HTML)) {
              continue;
            }
            // We shouldn't wrap invisible text node in new inline element.
            if (node->IsText() &&
                !HTMLEditUtils::IsVisibleTextNode(
                    *node->AsText(), TreatInvisibleLineBreakAs::Visible)) {
              continue;
            }
            arrayOfContentsAroundRange.AppendElement(*node->AsContent());
          }
        }
      }

      // If start node is a text node, apply new style to a part of it.
      if (range.StartRef().IsInTextNode() &&
          EditorUtils::IsEditableContent(*range.StartRef().ContainerAs<Text>(),
                                         EditorType::HTML)) {
        // MOZ_KnownLive(...ContainerAs<Text>()) because of grabbed by `range`.
        // MOZ_KnownLive(styleToSet.*) due to bug 1622253.
        Result<SplitRangeOffFromNodeResult, nsresult>
            wrapTextInStyledElementResult =
                inlineStyleSetter.SplitTextNodeAndApplyStyleToMiddleNode(
                    *this, MOZ_KnownLive(*range.StartRef().ContainerAs<Text>()),
                    range.StartRef().Offset(),
                    range.StartRef().ContainerAs<Text>()->TextDataLength());
        if (MOZ_UNLIKELY(wrapTextInStyledElementResult.isErr())) {
          NS_WARNING("HTMLEditor::SetInlinePropertyOnTextNode() failed");
          return wrapTextInStyledElementResult.unwrapErr();
        }
        // The caller should handle the ranges as Selection if necessary, and we
        // don't want to update aRanges with this result.
        wrapTextInStyledElementResult.inspect().IgnoreCaretPointSuggestion();
      }

      // Then, apply new style to all nodes in the range entirely.
      for (auto& content : arrayOfContentsAroundRange) {
        // MOZ_KnownLive due to bug 1622253.
        Result<CaretPoint, nsresult> pointToPutCaretOrError =
            inlineStyleSetter
                .ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle(
                    *this, MOZ_KnownLive(*content));
        if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
          NS_WARNING(
              "AutoInlineStyleSetter::"
              "ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle() failed");
          return pointToPutCaretOrError.unwrapErr();
        }
        // The caller should handle the ranges as Selection if necessary, and we
        // don't want to update aRanges with this result.
        pointToPutCaretOrError.inspect().IgnoreCaretPointSuggestion();
      }

      // Finally, if end node is a text node, apply new style to a part of it.
      if (range.EndRef().IsInTextNode() &&
          EditorUtils::IsEditableContent(*range.EndRef().ContainerAs<Text>(),
                                         EditorType::HTML)) {
        // MOZ_KnownLive(...ContainerAs<Text>()) because of grabbed by `range`.
        // MOZ_KnownLive(styleToSet.mAttribute) due to bug 1622253.
        Result<SplitRangeOffFromNodeResult, nsresult>
            wrapTextInStyledElementResult =
                inlineStyleSetter.SplitTextNodeAndApplyStyleToMiddleNode(
                    *this, MOZ_KnownLive(*range.EndRef().ContainerAs<Text>()),
                    0, range.EndRef().Offset());
        if (MOZ_UNLIKELY(wrapTextInStyledElementResult.isErr())) {
          NS_WARNING("HTMLEditor::SetInlinePropertyOnTextNode() failed");
          return wrapTextInStyledElementResult.unwrapErr();
        }
        // The caller should handle the ranges as Selection if necessary, and we
        // don't want to update aRanges with this result.
        wrapTextInStyledElementResult.inspect().IgnoreCaretPointSuggestion();
      }
      UpdateSelectionRange();
    }
  }
  return NS_OK;
}

// static
Result<RefPtr<Text>, nsresult>
HTMLEditor::AutoInlineStyleSetter::GetEmptyTextNodeToApplyNewStyle(
    HTMLEditor& aHTMLEditor, const EditorDOMPoint& aCandidatePointToInsert) {
  auto pointToInsertNewText =
      HTMLEditUtils::GetBetterCaretPositionToInsertText<EditorDOMPoint>(
          aCandidatePointToInsert);
  if (MOZ_UNLIKELY(!pointToInsertNewText.IsSet())) {
    return RefPtr<Text>();  // cannot insert text there
  }
  auto pointToInsertNewStyleOrError =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<EditorDOMPoint, nsresult> {
    if (!pointToInsertNewText.IsInTextNode()) {
      return pointToInsertNewText;
    }
    if (!pointToInsertNewText.ContainerAs<Text>()->TextDataLength()) {
      return pointToInsertNewText;  // Use it
    }
    if (pointToInsertNewText.IsStartOfContainer()) {
      return pointToInsertNewText.ParentPoint();
    }
    if (pointToInsertNewText.IsEndOfContainer()) {
      return EditorDOMPoint::After(*pointToInsertNewText.ContainerAs<Text>());
    }
    Result<SplitNodeResult, nsresult> splitTextNodeResult =
        aHTMLEditor.SplitNodeWithTransaction(pointToInsertNewText);
    if (MOZ_UNLIKELY(splitTextNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
      return splitTextNodeResult.propagateErr();
    }
    SplitNodeResult unwrappedSplitTextNodeResult = splitTextNodeResult.unwrap();
    unwrappedSplitTextNodeResult.IgnoreCaretPointSuggestion();
    return unwrappedSplitTextNodeResult.AtSplitPoint<EditorDOMPoint>();
  }();
  if (MOZ_UNLIKELY(pointToInsertNewStyleOrError.isErr())) {
    return pointToInsertNewStyleOrError.propagateErr();
  }

  // If we already have empty text node which is available for placeholder in
  // new styled element, let's use it.
  if (pointToInsertNewStyleOrError.inspect().IsInTextNode()) {
    return RefPtr<Text>(
        pointToInsertNewStyleOrError.inspect().ContainerAs<Text>());
  }

  // Otherwise, we need an empty text node to create new inline style.
  RefPtr<Text> newEmptyTextNode = aHTMLEditor.CreateTextNode(u""_ns);
  if (MOZ_UNLIKELY(!newEmptyTextNode)) {
    NS_WARNING("EditorBase::CreateTextNode() failed");
    return Err(NS_ERROR_FAILURE);
  }
  Result<CreateTextResult, nsresult> insertNewTextNodeResult =
      aHTMLEditor.InsertNodeWithTransaction<Text>(
          *newEmptyTextNode, pointToInsertNewStyleOrError.inspect());
  if (MOZ_UNLIKELY(insertNewTextNodeResult.isErr())) {
    NS_WARNING("EditorBase::InsertNodeWithTransaction() failed");
    return insertNewTextNodeResult.propagateErr();
  }
  insertNewTextNodeResult.inspect().IgnoreCaretPointSuggestion();
  return newEmptyTextNode;
}

Result<bool, nsresult>
HTMLEditor::AutoInlineStyleSetter::ElementIsGoodContainerForTheStyle(
    HTMLEditor& aHTMLEditor, Element& aElement) const {
  // If the editor is in the CSS mode and the style can be specified with CSS,
  // we should not use existing HTML element as a new container.
  const bool isCSSEditable = IsCSSSettable(aElement);
  if (!aHTMLEditor.IsCSSEnabled() || !isCSSEditable) {
    // First check for <b>, <i>, etc.
    if (aElement.IsHTMLElement(&HTMLPropertyRef()) &&
        !HTMLEditUtils::ElementHasAttribute(aElement) && !mAttribute) {
      return true;
    }

    // Now look for things like <font>
    if (mAttribute) {
      nsString attrValue;
      if (aElement.IsHTMLElement(&HTMLPropertyRef()) &&
          !HTMLEditUtils::ElementHasAttributeExcept(aElement, *mAttribute) &&
          aElement.GetAttr(mAttribute, attrValue)) {
        if (attrValue.Equals(mAttributeValue,
                             nsCaseInsensitiveStringComparator)) {
          return true;
        }
        if (mAttribute == nsGkAtoms::color ||
            mAttribute == nsGkAtoms::bgcolor) {
          if (aHTMLEditor.IsCSSEnabled()) {
            if (HTMLEditUtils::IsSameCSSColorValue(mAttributeValue,
                                                   attrValue)) {
              return true;
            }
          } else if (HTMLEditUtils::IsSameHTMLColorValue(
                         mAttributeValue, attrValue,
                         HTMLEditUtils::TransparentKeyword::Allowed)) {
            return true;
          }
        }
      }
    }

    if (!isCSSEditable) {
      return false;
    }
  }

  // No luck so far.  Now we check for a <span> with a single style=""
  // attribute that sets only the style we're looking for, if this type of
  // style supports it
  if (!aElement.IsHTMLElement(nsGkAtoms::span) ||
      !aElement.HasAttr(nsGkAtoms::style) ||
      HTMLEditUtils::ElementHasAttributeExcept(aElement, *nsGkAtoms::style)) {
    return false;
  }

  nsStyledElement* styledElement = nsStyledElement::FromNode(&aElement);
  if (MOZ_UNLIKELY(!styledElement)) {
    return false;
  }

  // Some CSS styles are not so simple.  For instance, underline is
  // "text-decoration: underline", which decomposes into four different text-*
  // properties.  So for now, we just create a span, add the desired style, and
  // see if it matches.
  RefPtr<Element> newSpanElement =
      aHTMLEditor.CreateHTMLContent(nsGkAtoms::span);
  if (MOZ_UNLIKELY(!newSpanElement)) {
    NS_WARNING("EditorBase::CreateHTMLContent(nsGkAtoms::span) failed");
    return false;
  }
  nsStyledElement* styledNewSpanElement =
      nsStyledElement::FromNode(newSpanElement);
  if (MOZ_UNLIKELY(!styledNewSpanElement)) {
    return false;
  }
  // MOZ_KnownLive(*styledNewSpanElement): It's newSpanElement whose type is
  // RefPtr.
  Result<size_t, nsresult> result = CSSEditUtils::SetCSSEquivalentToStyle(
      WithTransaction::No, aHTMLEditor, MOZ_KnownLive(*styledNewSpanElement),
      *this, &mAttributeValue);
  if (MOZ_UNLIKELY(result.isErr())) {
    // The call shouldn't return destroyed error because it must be
    // impossible to run script with modifying the new orphan node.
    MOZ_ASSERT_UNREACHABLE("How did you destroy this editor?");
    if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    return false;
  }
  return CSSEditUtils::DoStyledElementsHaveSameStyle(*styledNewSpanElement,
                                                     *styledElement);
}

bool HTMLEditor::AutoInlineStyleSetter::ElementIsGoodContainerToSetStyle(
    nsStyledElement& aStyledElement) const {
  if (!HTMLEditUtils::IsContainerNode(aStyledElement) ||
      !EditorUtils::IsEditableContent(aStyledElement, EditorType::HTML)) {
    return false;
  }

  // If it has `style` attribute, let's use it.
  if (aStyledElement.HasAttr(nsGkAtoms::style)) {
    return true;
  }

  // If it has `class` or `id` attribute, the element may have specific rule.
  // For applying the new style, we may need to set `style` attribute to it
  // to override the specified rule.
  if (aStyledElement.HasAttr(nsGkAtoms::id) ||
      aStyledElement.HasAttr(nsGkAtoms::_class)) {
    return true;
  }

  // If we're setting text-decoration and the element represents a value of
  // text-decoration, <ins> or <del>, let's use it.
  if (IsStyleOfTextDecoration(IgnoreSElement::No) &&
      aStyledElement.IsAnyOfHTMLElements(nsGkAtoms::u, nsGkAtoms::s,
                                         nsGkAtoms::strike, nsGkAtoms::ins,
                                         nsGkAtoms::del)) {
    return true;
  }

  // If we're setting font-size, color or background-color, we should use <font>
  // for compatibility with the other browsers.
  if (&HTMLPropertyRef() == nsGkAtoms::font &&
      aStyledElement.IsHTMLElement(nsGkAtoms::font)) {
    return true;
  }

  // If the element has one or more <br> (even if it's invisible), we don't
  // want to use the <span> for compatibility with the other browsers.
  if (aStyledElement.QuerySelector("br"_ns, IgnoreErrors())) {
    return false;
  }

  // NOTE: The following code does not match with the other browsers not
  //       completely.  Blink considers this with relation with the range.
  //       However, we cannot do it now.  We should fix here after or at
  //       fixing bug 1792386.

  // If it's only visible element child of parent block, let's use it.
  // E.g., we don't want to create new <span> when
  // `<p>{  <span>abc</span>  }</p>`.
  if (aStyledElement.GetParentElement() &&
      HTMLEditUtils::IsBlockElement(
          *aStyledElement.GetParentElement(),
          BlockInlineCheck::UseComputedDisplayStyle)) {
    for (nsIContent* previousSibling = aStyledElement.GetPreviousSibling();
         previousSibling;
         previousSibling = previousSibling->GetPreviousSibling()) {
      if (previousSibling->IsElement()) {
        return false;  // Assume any elements visible.
      }
      if (Text* text = Text::FromNode(previousSibling)) {
        if (HTMLEditUtils::IsVisibleTextNode(
                *text, TreatInvisibleLineBreakAs::Visible)) {
          return false;
        }
        continue;
      }
    }
    for (nsIContent* nextSibling = aStyledElement.GetNextSibling(); nextSibling;
         nextSibling = nextSibling->GetNextSibling()) {
      if (nextSibling->IsElement()) {
        if (!HTMLEditUtils::IsBRElementFollowedByBlockBoundary(*nextSibling)) {
          return false;
        }
        continue;  // The invisible <br> element may be followed by a child
                   // block, let's continue to check it.
      }
      if (Text* text = Text::FromNode(nextSibling)) {
        if (HTMLEditUtils::IsVisibleTextNode(
                *text, TreatInvisibleLineBreakAs::Visible)) {
          return false;
        }
        continue;
      }
    }
    return true;
  }

  // Otherwise, wrap it into new <span> for making
  // `<span>[abc</span> <span>def]</span>` become
  // `<span style="..."><span>abc</span> <span>def</span></span>` rather
  // than `<span style="...">abc <span>def</span></span>`.
  return false;
}

Result<SplitRangeOffFromNodeResult, nsresult>
HTMLEditor::AutoInlineStyleSetter::SplitTextNodeAndApplyStyleToMiddleNode(
    HTMLEditor& aHTMLEditor, Text& aText, uint32_t aStartOffset,
    uint32_t aEndOffset) {
  const RefPtr<Element> element = aText.GetParentElement();
  if (!element || !HTMLEditUtils::CanNodeContain(*element, HTMLPropertyRef())) {
    OnHandled(EditorDOMPoint(&aText, aStartOffset),
              EditorDOMPoint(&aText, aEndOffset));
    return SplitRangeOffFromNodeResult(nullptr, &aText, nullptr);
  }

  // Don't need to do anything if no characters actually selected
  if (aStartOffset == aEndOffset) {
    OnHandled(EditorDOMPoint(&aText, aStartOffset),
              EditorDOMPoint(&aText, aEndOffset));
    return SplitRangeOffFromNodeResult(nullptr, &aText, nullptr);
  }

  // Don't need to do anything if property already set on node
  if (IsCSSSettable(*element)) {
    // The HTML styles defined by this have a CSS equivalence for node;
    // let's check if it carries those CSS styles
    nsAutoString value(mAttributeValue);
    Result<bool, nsresult> isComputedCSSEquivalentToStyleOrError =
        CSSEditUtils::IsComputedCSSEquivalentTo(aHTMLEditor, *element, *this,
                                                value);
    if (MOZ_UNLIKELY(isComputedCSSEquivalentToStyleOrError.isErr())) {
      NS_WARNING("CSSEditUtils::IsComputedCSSEquivalentTo() failed");
      return isComputedCSSEquivalentToStyleOrError.propagateErr();
    }
    if (isComputedCSSEquivalentToStyleOrError.unwrap()) {
      OnHandled(EditorDOMPoint(&aText, aStartOffset),
                EditorDOMPoint(&aText, aEndOffset));
      return SplitRangeOffFromNodeResult(nullptr, &aText, nullptr);
    }
  } else if (HTMLEditUtils::IsInlineStyleSetByElement(aText, *this,
                                                      &mAttributeValue)) {
    OnHandled(EditorDOMPoint(&aText, aStartOffset),
              EditorDOMPoint(&aText, aEndOffset));
    return SplitRangeOffFromNodeResult(nullptr, &aText, nullptr);
  }

  // Make the range an independent node.
  auto splitAtEndResult =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<SplitNodeResult, nsresult> {
    EditorDOMPoint atEnd(&aText, aEndOffset);
    if (atEnd.IsEndOfContainer()) {
      return SplitNodeResult::NotHandled(atEnd);
    }
    // We need to split off back of text node
    Result<SplitNodeResult, nsresult> splitNodeResult =
        aHTMLEditor.SplitNodeWithTransaction(atEnd);
    if (splitNodeResult.isErr()) {
      NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
      return splitNodeResult;
    }
    if (MOZ_UNLIKELY(!splitNodeResult.inspect().HasCaretPointSuggestion())) {
      NS_WARNING(
          "HTMLEditor::SplitNodeWithTransaction() didn't suggest caret "
          "point");
      return Err(NS_ERROR_FAILURE);
    }
    return splitNodeResult;
  }();
  if (MOZ_UNLIKELY(splitAtEndResult.isErr())) {
    return splitAtEndResult.propagateErr();
  }
  SplitNodeResult unwrappedSplitAtEndResult = splitAtEndResult.unwrap();
  EditorDOMPoint pointToPutCaret = unwrappedSplitAtEndResult.UnwrapCaretPoint();
  auto splitAtStartResult =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<SplitNodeResult, nsresult> {
    EditorDOMPoint atStart(unwrappedSplitAtEndResult.DidSplit()
                               ? unwrappedSplitAtEndResult.GetPreviousContent()
                               : &aText,
                           aStartOffset);
    if (atStart.IsStartOfContainer()) {
      return SplitNodeResult::NotHandled(atStart);
    }
    // We need to split off front of text node
    Result<SplitNodeResult, nsresult> splitNodeResult =
        aHTMLEditor.SplitNodeWithTransaction(atStart);
    if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
      return splitNodeResult;
    }
    if (MOZ_UNLIKELY(!splitNodeResult.inspect().HasCaretPointSuggestion())) {
      NS_WARNING(
          "HTMLEditor::SplitNodeWithTransaction() didn't suggest caret "
          "point");
      return Err(NS_ERROR_FAILURE);
    }
    return splitNodeResult;
  }();
  if (MOZ_UNLIKELY(splitAtStartResult.isErr())) {
    return splitAtStartResult.propagateErr();
  }
  SplitNodeResult unwrappedSplitAtStartResult = splitAtStartResult.unwrap();
  if (unwrappedSplitAtStartResult.HasCaretPointSuggestion()) {
    pointToPutCaret = unwrappedSplitAtStartResult.UnwrapCaretPoint();
  }

  MOZ_ASSERT_IF(unwrappedSplitAtStartResult.DidSplit(),
                unwrappedSplitAtStartResult.GetPreviousContent()->IsText());
  MOZ_ASSERT_IF(unwrappedSplitAtStartResult.DidSplit(),
                unwrappedSplitAtStartResult.GetNextContent()->IsText());
  MOZ_ASSERT_IF(unwrappedSplitAtEndResult.DidSplit(),
                unwrappedSplitAtEndResult.GetPreviousContent()->IsText());
  MOZ_ASSERT_IF(unwrappedSplitAtEndResult.DidSplit(),
                unwrappedSplitAtEndResult.GetNextContent()->IsText());
  // Note that those text nodes are grabbed by unwrappedSplitAtStartResult,
  // unwrappedSplitAtEndResult or the callers.  Therefore, we don't need to make
  // them strong pointer.
  Text* const leftTextNode =
      unwrappedSplitAtStartResult.DidSplit()
          ? unwrappedSplitAtStartResult.GetPreviousContentAs<Text>()
          : nullptr;
  Text* const middleTextNode =
      unwrappedSplitAtStartResult.DidSplit()
          ? unwrappedSplitAtStartResult.GetNextContentAs<Text>()
          : (unwrappedSplitAtEndResult.DidSplit()
                 ? unwrappedSplitAtEndResult.GetPreviousContentAs<Text>()
                 : &aText);
  Text* const rightTextNode =
      unwrappedSplitAtEndResult.DidSplit()
          ? unwrappedSplitAtEndResult.GetNextContentAs<Text>()
          : nullptr;
  if (mAttribute) {
    // Look for siblings that are correct type of node
    nsIContent* sibling = HTMLEditUtils::GetPreviousSibling(
        *middleTextNode, {LeafNodeOption::IgnoreNonEditableNode},
        BlockInlineCheck::UseComputedDisplayOutsideStyle);
    if (sibling && sibling->IsElement()) {
      OwningNonNull<Element> element(*sibling->AsElement());
      Result<bool, nsresult> result =
          ElementIsGoodContainerForTheStyle(aHTMLEditor, element);
      if (MOZ_UNLIKELY(result.isErr())) {
        NS_WARNING("HTMLEditor::ElementIsGoodContainerForTheStyle() failed");
        return result.propagateErr();
      }
      if (result.inspect()) {
        // Previous sib is already right kind of inline node; slide this over
        Result<MoveNodeResult, nsresult> moveTextNodeResult =
            aHTMLEditor.MoveNodeToEndWithTransaction(
                MOZ_KnownLive(*middleTextNode), element);
        if (MOZ_UNLIKELY(moveTextNodeResult.isErr())) {
          NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
          return moveTextNodeResult.propagateErr();
        }
        MoveNodeResult unwrappedMoveTextNodeResult =
            moveTextNodeResult.unwrap();
        unwrappedMoveTextNodeResult.MoveCaretPointTo(
            pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
        OnHandled(*middleTextNode);
        return SplitRangeOffFromNodeResult(leftTextNode, middleTextNode,
                                           rightTextNode,
                                           std::move(pointToPutCaret));
      }
    }
    sibling = HTMLEditUtils::GetNextSibling(
        *middleTextNode, {LeafNodeOption::IgnoreNonEditableNode},
        BlockInlineCheck::UseComputedDisplayOutsideStyle);
    if (sibling && sibling->IsElement()) {
      OwningNonNull<Element> element(*sibling->AsElement());
      Result<bool, nsresult> result =
          ElementIsGoodContainerForTheStyle(aHTMLEditor, element);
      if (MOZ_UNLIKELY(result.isErr())) {
        NS_WARNING("HTMLEditor::ElementIsGoodContainerForTheStyle() failed");
        return result.propagateErr();
      }
      if (result.inspect()) {
        // Following sib is already right kind of inline node; slide this over
        Result<MoveNodeResult, nsresult> moveTextNodeResult =
            aHTMLEditor.MoveNodeWithTransaction(MOZ_KnownLive(*middleTextNode),
                                                EditorDOMPoint(sibling, 0u));
        if (MOZ_UNLIKELY(moveTextNodeResult.isErr())) {
          NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
          return moveTextNodeResult.propagateErr();
        }
        MoveNodeResult unwrappedMoveTextNodeResult =
            moveTextNodeResult.unwrap();
        unwrappedMoveTextNodeResult.MoveCaretPointTo(
            pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
        OnHandled(*middleTextNode);
        return SplitRangeOffFromNodeResult(leftTextNode, middleTextNode,
                                           rightTextNode,
                                           std::move(pointToPutCaret));
      }
    }
  }

  // Wrap the node inside inline node.
  Result<CaretPoint, nsresult> setStyleResult =
      ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle(
          aHTMLEditor, MOZ_KnownLive(*middleTextNode));
  if (MOZ_UNLIKELY(setStyleResult.isErr())) {
    NS_WARNING(
        "AutoInlineStyleSetter::"
        "ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle() failed");
    return setStyleResult.propagateErr();
  }
  return SplitRangeOffFromNodeResult(
      leftTextNode, middleTextNode, rightTextNode,
      setStyleResult.unwrap().UnwrapCaretPoint());
}

Result<CaretPoint, nsresult> HTMLEditor::AutoInlineStyleSetter::ApplyStyle(
    HTMLEditor& aHTMLEditor, nsIContent& aContent) {
  // If this is an element that can't be contained in a span, we have to
  // recurse to its children.
  if (!HTMLEditUtils::CanNodeContain(*nsGkAtoms::span, aContent)) {
    if (!aContent.HasChildren()) {
      return CaretPoint(EditorDOMPoint());
    }

    AutoTArray<OwningNonNull<nsIContent>, 32> arrayOfContents;
    HTMLEditUtils::CollectChildren(
        aContent, arrayOfContents,
        {CollectChildrenOption::IgnoreNonEditableChildren,
         CollectChildrenOption::IgnoreInvisibleTextNodes});

    // Then loop through the list, set the property on each node.
    EditorDOMPoint pointToPutCaret;
    for (const OwningNonNull<nsIContent>& content : arrayOfContents) {
      // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
      // keep it alive.
      Result<CaretPoint, nsresult> setInlinePropertyResult =
          ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle(
              aHTMLEditor, MOZ_KnownLive(content));
      if (MOZ_UNLIKELY(setInlinePropertyResult.isErr())) {
        NS_WARNING(
            "AutoInlineStyleSetter::"
            "ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle() failed");
        return setInlinePropertyResult;
      }
      setInlinePropertyResult.unwrap().MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    }
    return CaretPoint(std::move(pointToPutCaret));
  }

  // First check if there's an adjacent sibling we can put our node into.
  const nsCOMPtr<nsIContent> previousSibling =
      HTMLEditUtils::GetPreviousSibling(
          aContent, {LeafNodeOption::IgnoreNonEditableNode},
          BlockInlineCheck::UseComputedDisplayOutsideStyle);
  const nsCOMPtr<nsIContent> nextSibling = HTMLEditUtils::GetNextSibling(
      aContent, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (RefPtr<Element> previousElement =
          Element::FromNodeOrNull(previousSibling)) {
    Result<bool, nsresult> canMoveIntoPreviousSibling =
        ElementIsGoodContainerForTheStyle(aHTMLEditor, *previousElement);
    if (MOZ_UNLIKELY(canMoveIntoPreviousSibling.isErr())) {
      NS_WARNING("HTMLEditor::ElementIsGoodContainerForTheStyle() failed");
      return canMoveIntoPreviousSibling.propagateErr();
    }
    if (canMoveIntoPreviousSibling.inspect()) {
      Result<MoveNodeResult, nsresult> moveNodeResult =
          aHTMLEditor.MoveNodeToEndWithTransaction(aContent, *previousSibling);
      if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
        return moveNodeResult.propagateErr();
      }
      MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
      RefPtr<Element> nextElement = Element::FromNodeOrNull(nextSibling);
      if (!nextElement) {
        OnHandled(aContent);
        return CaretPoint(unwrappedMoveNodeResult.UnwrapCaretPoint());
      }
      Result<bool, nsresult> canMoveIntoNextSibling =
          ElementIsGoodContainerForTheStyle(aHTMLEditor, *nextElement);
      if (MOZ_UNLIKELY(canMoveIntoNextSibling.isErr())) {
        NS_WARNING("HTMLEditor::ElementIsGoodContainerForTheStyle() failed");
        unwrappedMoveNodeResult.IgnoreCaretPointSuggestion();
        return canMoveIntoNextSibling.propagateErr();
      }
      if (!canMoveIntoNextSibling.inspect()) {
        OnHandled(aContent);
        return CaretPoint(unwrappedMoveNodeResult.UnwrapCaretPoint());
      }
      unwrappedMoveNodeResult.IgnoreCaretPointSuggestion();

      // JoinNodesWithTransaction (DoJoinNodes) tries to collapse selection to
      // the joined point and we want to skip updating `Selection` here.
      AutoTransactionsConserveSelection dontChangeMySelection(aHTMLEditor);
      Result<JoinNodesResult, nsresult> joinNodesResult =
          aHTMLEditor.JoinNodesWithTransaction(*previousElement, *nextElement);
      if (MOZ_UNLIKELY(joinNodesResult.isErr())) {
        NS_WARNING("HTMLEditor::JoinNodesWithTransaction() failed");
        return joinNodesResult.propagateErr();
      }
      // So, let's take it.
      OnHandled(aContent);
      return CaretPoint(
          joinNodesResult.inspect().AtJoinedPoint<EditorDOMPoint>());
    }
  }

  if (RefPtr<Element> nextElement = Element::FromNodeOrNull(nextSibling)) {
    Result<bool, nsresult> canMoveIntoNextSibling =
        ElementIsGoodContainerForTheStyle(aHTMLEditor, *nextElement);
    if (MOZ_UNLIKELY(canMoveIntoNextSibling.isErr())) {
      NS_WARNING("HTMLEditor::ElementIsGoodContainerForTheStyle() failed");
      return canMoveIntoNextSibling.propagateErr();
    }
    if (canMoveIntoNextSibling.inspect()) {
      Result<MoveNodeResult, nsresult> moveNodeResult =
          aHTMLEditor.MoveNodeWithTransaction(aContent,
                                              EditorDOMPoint(nextElement, 0u));
      if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
        return moveNodeResult.propagateErr();
      }
      OnHandled(aContent);
      return CaretPoint(moveNodeResult.unwrap().UnwrapCaretPoint());
    }
  }

  // Don't need to do anything if property already set on node
  if (const RefPtr<Element> element = aContent.GetAsElementOrParentElement()) {
    if (IsCSSSettable(*element)) {
      nsAutoString value(mAttributeValue);
      // MOZ_KnownLive(element) because it's aContent.
      Result<bool, nsresult> isComputedCSSEquivalentToStyleOrError =
          CSSEditUtils::IsComputedCSSEquivalentTo(aHTMLEditor, *element, *this,
                                                  value);
      if (MOZ_UNLIKELY(isComputedCSSEquivalentToStyleOrError.isErr())) {
        NS_WARNING("CSSEditUtils::IsComputedCSSEquivalentTo() failed");
        return isComputedCSSEquivalentToStyleOrError.propagateErr();
      }
      if (isComputedCSSEquivalentToStyleOrError.unwrap()) {
        OnHandled(aContent);
        return CaretPoint(EditorDOMPoint());
      }
    } else if (HTMLEditUtils::IsInlineStyleSetByElement(*element, *this,
                                                        &mAttributeValue)) {
      OnHandled(aContent);
      return CaretPoint(EditorDOMPoint());
    }
  }

  auto ShouldUseCSS = [&]() {
    if (aHTMLEditor.IsCSSEnabled() && aContent.GetAsElementOrParentElement() &&
        IsCSSSettable(*aContent.GetAsElementOrParentElement())) {
      return true;
    }
    // bgcolor is always done using CSS
    if (mAttribute == nsGkAtoms::bgcolor) {
      return true;
    }
    // called for removing parent style, we should use CSS with <span> element.
    if (IsStyleToInvert()) {
      return true;
    }
    // If we set color value, the value may be able to specified only with CSS.
    // In that case, we need to use CSS even in the HTML mode.
    if (mAttribute == nsGkAtoms::color) {
      return mAttributeValue.First() != '#' &&
             !HTMLEditUtils::CanConvertToHTMLColorValue(mAttributeValue);
    }
    return false;
  };

  if (ShouldUseCSS()) {
    // We need special handlings for text-decoration.
    if (IsStyleOfTextDecoration(IgnoreSElement::No)) {
      Result<CaretPoint, nsresult> result =
          ApplyCSSTextDecoration(aHTMLEditor, aContent);
      NS_WARNING_ASSERTION(
          result.isOk(),
          "AutoInlineStyleSetter::ApplyCSSTextDecoration() failed");
      return result;
    }
    EditorDOMPoint pointToPutCaret;
    RefPtr<nsStyledElement> styledElement = [&]() -> nsStyledElement* {
      auto* const styledElement = nsStyledElement::FromNode(&aContent);
      return styledElement && ElementIsGoodContainerToSetStyle(*styledElement)
                 ? styledElement
                 : nullptr;
    }();

    // If we don't have good element to set the style, let's create new <span>.
    if (!styledElement) {
      Result<CreateElementResult, nsresult> wrapInSpanElementResult =
          aHTMLEditor.InsertContainerWithTransaction(aContent,
                                                     *nsGkAtoms::span);
      if (MOZ_UNLIKELY(wrapInSpanElementResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::InsertContainerWithTransaction(nsGkAtoms::span) "
            "failed");
        return wrapInSpanElementResult.propagateErr();
      }
      CreateElementResult unwrappedWrapInSpanElementResult =
          wrapInSpanElementResult.unwrap();
      MOZ_ASSERT(unwrappedWrapInSpanElementResult.GetNewNode());
      unwrappedWrapInSpanElementResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      styledElement = nsStyledElement::FromNode(
          unwrappedWrapInSpanElementResult.GetNewNode());
      MOZ_ASSERT(styledElement);
      if (MOZ_UNLIKELY(!styledElement)) {
        // Don't return error to avoid creating new path to throwing error.
        OnHandled(aContent);
        return CaretPoint(pointToPutCaret);
      }
    }

    // Add the CSS styles corresponding to the HTML style request
    if (IsCSSSettable(*styledElement)) {
      Result<size_t, nsresult> result = CSSEditUtils::SetCSSEquivalentToStyle(
          WithTransaction::Yes, aHTMLEditor, *styledElement, *this,
          &mAttributeValue);
      if (MOZ_UNLIKELY(result.isErr())) {
        if (NS_WARN_IF(result.inspectErr() == NS_ERROR_EDITOR_DESTROYED)) {
          return Err(NS_ERROR_EDITOR_DESTROYED);
        }
        NS_WARNING(
            "CSSEditUtils::SetCSSEquivalentToStyle() failed, but ignored");
      }
    }
    OnHandled(aContent);
    return CaretPoint(pointToPutCaret);
  }

  nsAutoString attributeValue(mAttributeValue);
  if (mAttribute == nsGkAtoms::color && mAttributeValue.First() != '#') {
    // At here, all color values should be able to be parsed as a CSS color
    // value.  Therefore, we need to convert it to normalized HTML color value.
    HTMLEditUtils::ConvertToNormalizedHTMLColorValue(attributeValue,
                                                     attributeValue);
  }

  // is it already the right kind of node, but with wrong attribute?
  if (aContent.IsHTMLElement(&HTMLPropertyRef())) {
    if (NS_WARN_IF(!mAttribute)) {
      return Err(NS_ERROR_INVALID_ARG);
    }
    // Just set the attribute on it.
    nsresult rv = aHTMLEditor.SetAttributeWithTransaction(
        MOZ_KnownLive(*aContent.AsElement()), *mAttribute, attributeValue);
    if (NS_WARN_IF(aHTMLEditor.Destroyed())) {
      return Err(NS_ERROR_EDITOR_DESTROYED);
    }
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::SetAttributeWithTransaction() failed");
      return Err(rv);
    }
    OnHandled(aContent);
    return CaretPoint(EditorDOMPoint());
  }

  // ok, chuck it in its very own container
  Result<CreateElementResult, nsresult> wrapWithNewElementToFormatResult =
      aHTMLEditor.InsertContainerWithTransaction(
          aContent, MOZ_KnownLive(HTMLPropertyRef()),
          !mAttribute ? HTMLEditor::DoNothingForNewElement
                      // MOZ_CAN_RUN_SCRIPT_BOUNDARY due to bug 1758868
                      : [&](HTMLEditor& /*aHTMLEditor*/, Element& aNewElement,
                            const EditorDOMPoint&) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
                          nsresult rv = aNewElement.SetAttr(
                              kNameSpaceID_None, mAttribute, attributeValue,
                              false, IsKnownNewAttr::Yes);
                          NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                               "Element::SetAttr() failed");
                          return rv;
                        });
  if (MOZ_UNLIKELY(wrapWithNewElementToFormatResult.isErr())) {
    NS_WARNING("HTMLEditor::InsertContainerWithTransaction() failed");
    return wrapWithNewElementToFormatResult.propagateErr();
  }
  OnHandled(aContent);
  MOZ_ASSERT(wrapWithNewElementToFormatResult.inspect().GetNewNode());
  return CaretPoint(
      wrapWithNewElementToFormatResult.unwrap().UnwrapCaretPoint());
}

Result<CaretPoint, nsresult>
HTMLEditor::AutoInlineStyleSetter::ApplyCSSTextDecoration(
    HTMLEditor& aHTMLEditor, nsIContent& aContent) {
  MOZ_ASSERT(IsStyleOfTextDecoration(IgnoreSElement::No));

  EditorDOMPoint pointToPutCaret;
  RefPtr<nsStyledElement> styledElement = nsStyledElement::FromNode(aContent);
  nsAutoString newTextDecorationValue;
  if (&HTMLPropertyRef() == nsGkAtoms::u) {
    newTextDecorationValue.AssignLiteral(u"underline");
  } else if (&HTMLPropertyRef() == nsGkAtoms::s ||
             &HTMLPropertyRef() == nsGkAtoms::strike) {
    newTextDecorationValue.AssignLiteral(u"line-through");
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "Was new value added in "
        "IsStyleOfTextDecoration(IgnoreSElement::No))?");
  }
  if (styledElement && IsCSSSettable(*styledElement) &&
      ElementIsGoodContainerToSetStyle(*styledElement)) {
    nsAutoString textDecorationValue;
    nsresult rv = CSSEditUtils::GetSpecifiedProperty(
        *styledElement, *nsGkAtoms::text_decoration, textDecorationValue);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "CSSEditUtils::GetSpecifiedProperty(nsGkAtoms::text_decoration) "
          "failed");
      return Err(rv);
    }
    // However, if the element is an element to style the text-decoration,
    // replace it with new <span>.
    if (styledElement && styledElement->IsAnyOfHTMLElements(
                             nsGkAtoms::u, nsGkAtoms::s, nsGkAtoms::strike)) {
      Result<CreateElementResult, nsresult> replaceResult =
          aHTMLEditor.ReplaceContainerAndCloneAttributesWithTransaction(
              *styledElement, *nsGkAtoms::span);
      if (MOZ_UNLIKELY(replaceResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::ReplaceContainerAndCloneAttributesWithTransaction() "
            "failed");
        return replaceResult.propagateErr();
      }
      CreateElementResult unwrappedReplaceResult = replaceResult.unwrap();
      MOZ_ASSERT(unwrappedReplaceResult.GetNewNode());
      unwrappedReplaceResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      // The new <span> needs to specify the original element's text-decoration
      // style unless it's specified explicitly.
      if (textDecorationValue.IsEmpty()) {
        if (!newTextDecorationValue.IsEmpty()) {
          newTextDecorationValue.Append(HTMLEditUtils::kSpace);
        }
        if (styledElement->IsHTMLElement(nsGkAtoms::u)) {
          newTextDecorationValue.AppendLiteral(u"underline");
        } else {
          newTextDecorationValue.AppendLiteral(u"line-through");
        }
      }
      styledElement =
          nsStyledElement::FromNode(unwrappedReplaceResult.GetNewNode());
      if (NS_WARN_IF(!styledElement)) {
        OnHandled(aContent);
        return CaretPoint(pointToPutCaret);
      }
    }
    // If the element has default style, we need to keep it after specifying
    // text-decoration.
    else if (textDecorationValue.IsEmpty() &&
             styledElement->IsAnyOfHTMLElements(nsGkAtoms::u, nsGkAtoms::ins)) {
      if (!newTextDecorationValue.IsEmpty()) {
        newTextDecorationValue.Append(HTMLEditUtils::kSpace);
      }
      newTextDecorationValue.AppendLiteral(u"underline");
    } else if (textDecorationValue.IsEmpty() &&
               styledElement->IsAnyOfHTMLElements(
                   nsGkAtoms::s, nsGkAtoms::strike, nsGkAtoms::del)) {
      if (!newTextDecorationValue.IsEmpty()) {
        newTextDecorationValue.Append(HTMLEditUtils::kSpace);
      }
      newTextDecorationValue.AppendLiteral(u"line-through");
    }
  }
  // Otherwise, use new <span> element.
  else {
    Result<CreateElementResult, nsresult> wrapInSpanElementResult =
        aHTMLEditor.InsertContainerWithTransaction(aContent, *nsGkAtoms::span);
    if (MOZ_UNLIKELY(wrapInSpanElementResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertContainerWithTransaction(nsGkAtoms::span) failed");
      return wrapInSpanElementResult.propagateErr();
    }
    CreateElementResult unwrappedWrapInSpanElementResult =
        wrapInSpanElementResult.unwrap();
    MOZ_ASSERT(unwrappedWrapInSpanElementResult.GetNewNode());
    unwrappedWrapInSpanElementResult.MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    styledElement = nsStyledElement::FromNode(
        unwrappedWrapInSpanElementResult.GetNewNode());
    if (NS_WARN_IF(!styledElement)) {
      OnHandled(aContent);
      return CaretPoint(pointToPutCaret);
    }
  }

  nsresult rv = CSSEditUtils::SetCSSPropertyWithTransaction(
      aHTMLEditor, *styledElement, *nsGkAtoms::text_decoration,
      newTextDecorationValue);
  if (NS_FAILED(rv)) {
    NS_WARNING("CSSEditUtils::SetCSSPropertyWithTransaction() failed");
    return Err(rv);
  }
  OnHandled(aContent);
  return CaretPoint(pointToPutCaret);
}

Result<CaretPoint, nsresult> HTMLEditor::AutoInlineStyleSetter::
    ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle(HTMLEditor& aHTMLEditor,
                                                       nsIContent& aContent) {
  if (NS_WARN_IF(!aContent.GetParentNode())) {
    return Err(NS_ERROR_FAILURE);
  }
  OwningNonNull<nsINode> parent = *aContent.GetParentNode();
  nsCOMPtr<nsIContent> previousSibling = aContent.GetPreviousSibling(),
                       nextSibling = aContent.GetNextSibling();
  EditorDOMPoint pointToPutCaret;
  if (aContent.IsElement()) {
    Result<EditorDOMPoint, nsresult> removeStyleResult =
        aHTMLEditor.RemoveStyleInside(MOZ_KnownLive(*aContent.AsElement()),
                                      *this, SpecifiedStyle::Preserve);
    if (MOZ_UNLIKELY(removeStyleResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveStyleInside() failed");
      return removeStyleResult.propagateErr();
    }
    if (removeStyleResult.inspect().IsSet()) {
      pointToPutCaret = removeStyleResult.unwrap();
    }
    if (nsStaticAtom* similarElementNameAtom = GetSimilarElementNameAtom()) {
      Result<EditorDOMPoint, nsresult> removeStyleResult =
          aHTMLEditor.RemoveStyleInside(
              MOZ_KnownLive(*aContent.AsElement()),
              EditorInlineStyle(*similarElementNameAtom),
              SpecifiedStyle::Preserve);
      if (MOZ_UNLIKELY(removeStyleResult.isErr())) {
        NS_WARNING("HTMLEditor::RemoveStyleInside() failed");
        return removeStyleResult.propagateErr();
      }
      if (removeStyleResult.inspect().IsSet()) {
        pointToPutCaret = removeStyleResult.unwrap();
      }
    }
  }

  if (aContent.GetParentNode()) {
    // The node is still where it was
    Result<CaretPoint, nsresult> pointToPutCaretOrError =
        ApplyStyle(aHTMLEditor, aContent);
    NS_WARNING_ASSERTION(pointToPutCaretOrError.isOk(),
                         "AutoInlineStyleSetter::ApplyStyle() failed");
    return pointToPutCaretOrError;
  }

  // It's vanished.  Use the old siblings for reference to construct a
  // list.  But first, verify that the previous/next siblings are still
  // where we expect them; otherwise we have to give up.
  if (NS_WARN_IF(previousSibling &&
                 previousSibling->GetParentNode() != parent) ||
      NS_WARN_IF(nextSibling && nextSibling->GetParentNode() != parent) ||
      NS_WARN_IF(!parent->IsInComposedDoc())) {
    return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
  }
  AutoTArray<OwningNonNull<nsIContent>, 24> nodesToSet;
  for (nsIContent* content = previousSibling ? previousSibling->GetNextSibling()
                                             : parent->GetFirstChild();
       content && content != nextSibling; content = content->GetNextSibling()) {
    if (EditorUtils::IsEditableContent(*content, EditorType::HTML)) {
      nodesToSet.AppendElement(*content);
    }
  }

  for (OwningNonNull<nsIContent>& content : nodesToSet) {
    // MOZ_KnownLive because 'nodesToSet' is guaranteed to
    // keep it alive.
    Result<CaretPoint, nsresult> pointToPutCaretOrError =
        ApplyStyle(aHTMLEditor, MOZ_KnownLive(content));
    if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
      NS_WARNING("AutoInlineStyleSetter::ApplyStyle() failed");
      return pointToPutCaretOrError;
    }
    pointToPutCaretOrError.unwrap().MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
  }

  return CaretPoint(pointToPutCaret);
}

bool HTMLEditor::AutoInlineStyleSetter::ContentIsElementSettingTheStyle(
    const HTMLEditor& aHTMLEditor, nsIContent& aContent) const {
  Element* const element = Element::FromNode(&aContent);
  if (!element) {
    return false;
  }
  if (IsRepresentedBy(*element)) {
    return true;
  }
  Result<bool, nsresult> specified = IsSpecifiedBy(aHTMLEditor, *element);
  NS_WARNING_ASSERTION(specified.isOk(),
                       "EditorInlineStyle::IsSpecified() failed, but ignored");
  return specified.unwrapOr(false);
}

// static
nsIContent* HTMLEditor::AutoInlineStyleSetter::GetNextEditableInlineContent(
    const nsIContent& aContent, const nsINode* aLimiter) {
  auto* const nextContentInRange = [&]() -> nsIContent* {
    for (nsIContent* parent : aContent.InclusiveAncestorsOfType<nsIContent>()) {
      if (parent == aLimiter ||
          !EditorUtils::IsEditableContent(*parent, EditorType::HTML) ||
          (parent->IsElement() &&
           (HTMLEditUtils::IsBlockElement(
                *parent->AsElement(),
                BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
            HTMLEditUtils::IsDisplayInsideFlowRoot(*parent->AsElement())))) {
        return nullptr;
      }
      if (nsIContent* nextSibling = parent->GetNextSibling()) {
        return nextSibling;
      }
    }
    return nullptr;
  }();
  return nextContentInRange &&
                 EditorUtils::IsEditableContent(*nextContentInRange,
                                                EditorType::HTML) &&
                 !HTMLEditUtils::IsBlockElement(
                     *nextContentInRange,
                     BlockInlineCheck::UseComputedDisplayOutsideStyle)
             ? nextContentInRange
             : nullptr;
}

// static
nsIContent* HTMLEditor::AutoInlineStyleSetter::GetPreviousEditableInlineContent(
    const nsIContent& aContent, const nsINode* aLimiter) {
  auto* const previousContentInRange = [&]() -> nsIContent* {
    for (nsIContent* parent : aContent.InclusiveAncestorsOfType<nsIContent>()) {
      if (parent == aLimiter ||
          !EditorUtils::IsEditableContent(*parent, EditorType::HTML) ||
          (parent->IsElement() &&
           (HTMLEditUtils::IsBlockElement(
                *parent->AsElement(),
                BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
            HTMLEditUtils::IsDisplayInsideFlowRoot(*parent->AsElement())))) {
        return nullptr;
      }
      if (nsIContent* previousSibling = parent->GetPreviousSibling()) {
        return previousSibling;
      }
    }
    return nullptr;
  }();
  return previousContentInRange &&
                 EditorUtils::IsEditableContent(*previousContentInRange,
                                                EditorType::HTML) &&
                 !HTMLEditUtils::IsBlockElement(
                     *previousContentInRange,
                     BlockInlineCheck::UseComputedDisplayOutsideStyle)
             ? previousContentInRange
             : nullptr;
}

EditorRawDOMPoint HTMLEditor::AutoInlineStyleSetter::GetShrunkenRangeStart(
    const HTMLEditor& aHTMLEditor, const EditorDOMRange& aRange,
    const nsINode& aCommonAncestorOfRange,
    const nsIContent* aFirstEntirelySelectedContentNodeInRange) const {
  const EditorDOMPoint& startRef = aRange.StartRef();
  // <a> cannot be nested and it should be represented with one element as far
  // as possible.  Therefore, we don't need to shrink the range.
  if (IsStyleOfAnchorElement()) {
    return startRef.To<EditorRawDOMPoint>();
  }
  // If the start boundary is at end of a node, we need to shrink the range
  // to next content, e.g., `abc[<b>def` should be `abc<b>[def` unless the
  // <b> is not entirely selected.
  auto* const nextContentOrStartContainer = [&]() -> nsIContent* {
    if (!startRef.IsInContentNode()) {
      return nullptr;
    }
    if (!startRef.IsEndOfContainer()) {
      return startRef.ContainerAs<nsIContent>();
    }
    nsIContent* const nextContent =
        AutoInlineStyleSetter::GetNextEditableInlineContent(
            *startRef.ContainerAs<nsIContent>(), &aCommonAncestorOfRange);
    return nextContent ? nextContent : startRef.ContainerAs<nsIContent>();
  }();
  if (MOZ_UNLIKELY(!nextContentOrStartContainer)) {
    return startRef.To<EditorRawDOMPoint>();
  }
  EditorRawDOMPoint startPoint =
      nextContentOrStartContainer != startRef.ContainerAs<nsIContent>()
          ? EditorRawDOMPoint(nextContentOrStartContainer)
          : startRef.To<EditorRawDOMPoint>();
  MOZ_ASSERT(startPoint.IsSet());
  // If the start point points a content node, let's try to move it down to
  // start of the child recursively.
  while (nsIContent* child = startPoint.GetChild()) {
    // We shouldn't cross editable and block boundary.
    if (!EditorUtils::IsEditableContent(*child, EditorType::HTML) ||
        HTMLEditUtils::IsBlockElement(
            *child, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      break;
    }
    // If we reach a text node, the minimized range starts from start of it.
    if (child->IsText()) {
      startPoint.Set(child, 0u);
      break;
    }
    // Don't shrink the range into element which applies the style to children
    // because we want to update the element.  E.g., if we are setting
    // background color, we want to update style attribute of an element which
    // specifies background color with `style` attribute.
    if (child == aFirstEntirelySelectedContentNodeInRange) {
      break;
    }
    // We should not start from an atomic element such as <br>, <img>, etc.
    if (!HTMLEditUtils::IsContainerNode(*child)) {
      break;
    }
    // If the element specifies the style, we should update it.  Therefore, we
    // need to wrap it in the range.
    if (ContentIsElementSettingTheStyle(aHTMLEditor, *child)) {
      break;
    }
    // If the child is an `<a>`, we should not shrink the range into it
    // because user may not want to keep editing in the link except when user
    // tries to update selection into it obviously.
    if (child->IsHTMLElement(nsGkAtoms::a)) {
      break;
    }
    startPoint.Set(child, 0u);
  }
  return startPoint;
}

EditorRawDOMPoint HTMLEditor::AutoInlineStyleSetter::GetShrunkenRangeEnd(
    const HTMLEditor& aHTMLEditor, const EditorDOMRange& aRange,
    const nsINode& aCommonAncestorOfRange,
    const nsIContent* aLastEntirelySelectedContentNodeInRange) const {
  const EditorDOMPoint& endRef = aRange.EndRef();
  // <a> cannot be nested and it should be represented with one element as far
  // as possible.  Therefore, we don't need to shrink the range.
  if (IsStyleOfAnchorElement()) {
    return endRef.To<EditorRawDOMPoint>();
  }
  // If the end boundary is at start of a node, we need to shrink the range
  // to previous content, e.g., `abc</b>]def` should be `abc]</b>def` unless
  // the <b> is not entirely selected.
  auto* const previousContentOrEndContainer = [&]() -> nsIContent* {
    if (!endRef.IsInContentNode()) {
      return nullptr;
    }
    if (!endRef.IsStartOfContainer()) {
      return endRef.ContainerAs<nsIContent>();
    }
    nsIContent* const previousContent =
        AutoInlineStyleSetter::GetPreviousEditableInlineContent(
            *endRef.ContainerAs<nsIContent>(), &aCommonAncestorOfRange);
    return previousContent ? previousContent : endRef.ContainerAs<nsIContent>();
  }();
  if (MOZ_UNLIKELY(!previousContentOrEndContainer)) {
    return endRef.To<EditorRawDOMPoint>();
  }
  EditorRawDOMPoint endPoint =
      previousContentOrEndContainer != endRef.ContainerAs<nsIContent>()
          ? EditorRawDOMPoint::After(*previousContentOrEndContainer)
          : endRef.To<EditorRawDOMPoint>();
  MOZ_ASSERT(endPoint.IsSet());
  // If the end point points after a content node, let's try to move it down
  // to end of the child recursively.
  while (nsIContent* child = endPoint.GetPreviousSiblingOfChild()) {
    // We shouldn't cross editable and block boundary.
    if (!EditorUtils::IsEditableContent(*child, EditorType::HTML) ||
        HTMLEditUtils::IsBlockElement(
            *child, BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      break;
    }
    // If we reach a text node, the minimized range starts from start of it.
    if (child->IsText()) {
      endPoint.SetToEndOf(child);
      break;
    }
    // Don't shrink the range into element which applies the style to children
    // because we want to update the element.  E.g., if we are setting
    // background color, we want to update style attribute of an element which
    // specifies background color with `style` attribute.
    if (child == aLastEntirelySelectedContentNodeInRange) {
      break;
    }
    // We should not end in an atomic element such as <br>, <img>, etc.
    if (!HTMLEditUtils::IsContainerNode(*child)) {
      break;
    }
    // If the element specifies the style, we should update it.  Therefore, we
    // need to wrap it in the range.
    if (ContentIsElementSettingTheStyle(aHTMLEditor, *child)) {
      break;
    }
    // If the child is an `<a>`, we should not shrink the range into it
    // because user may not want to keep editing in the link except when user
    // tries to update selection into it obviously.
    if (child->IsHTMLElement(nsGkAtoms::a)) {
      break;
    }
    endPoint.SetToEndOf(child);
  }
  return endPoint;
}

EditorRawDOMPoint HTMLEditor::AutoInlineStyleSetter::
    GetExtendedRangeStartToWrapAncestorApplyingSameStyle(
        const HTMLEditor& aHTMLEditor,
        const EditorRawDOMPoint& aStartPoint) const {
  MOZ_ASSERT(aStartPoint.IsSetAndValid());

  EditorRawDOMPoint startPoint = aStartPoint;
  // FIXME: This should handle ignore invisible white-spaces before the position
  // if it's in a text node or invisible white-spaces.
  if (!startPoint.IsStartOfContainer() ||
      startPoint.GetContainer()->GetPreviousSibling()) {
    return startPoint;
  }

  // FYI: Currently, we don't support setting `font-size` even in the CSS mode.
  // Therefore, if the style is <font size="...">, we always set a <font>.
  const bool isSettingFontElement =
      IsStyleOfFontSize() ||
      (!aHTMLEditor.IsCSSEnabled() && IsStyleOfFontElement());
  Element* mostDistantStartParentHavingStyle = nullptr;
  for (Element* parent :
       startPoint.GetContainer()->InclusiveAncestorsOfType<Element>()) {
    if (!EditorUtils::IsEditableContent(*parent, EditorType::HTML) ||
        HTMLEditUtils::IsBlockElement(
            *parent, BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
        HTMLEditUtils::IsDisplayInsideFlowRoot(*parent)) {
      break;
    }
    if (ContentIsElementSettingTheStyle(aHTMLEditor, *parent)) {
      mostDistantStartParentHavingStyle = parent;
    }
    // If we're setting <font> element and there is a <font> element which is
    // entirely selected, we should use it.
    else if (isSettingFontElement && parent->IsHTMLElement(nsGkAtoms::font)) {
      mostDistantStartParentHavingStyle = parent;
    }
    if (parent->GetPreviousSibling()) {
      break;  // The parent is not first element in its parent, stop climbing.
    }
  }
  if (mostDistantStartParentHavingStyle) {
    startPoint.Set(mostDistantStartParentHavingStyle);
  }
  return startPoint;
}

EditorRawDOMPoint HTMLEditor::AutoInlineStyleSetter::
    GetExtendedRangeEndToWrapAncestorApplyingSameStyle(
        const HTMLEditor& aHTMLEditor,
        const EditorRawDOMPoint& aEndPoint) const {
  MOZ_ASSERT(aEndPoint.IsSetAndValid());

  EditorRawDOMPoint endPoint = aEndPoint;
  // FIXME: This should ignore invisible white-spaces after the position if it's
  // in a text node, invisible <br> or following invisible text nodes.
  if (!endPoint.IsEndOfContainer() ||
      endPoint.GetContainer()->GetNextSibling()) {
    return endPoint;
  }

  // FYI: Currently, we don't support setting `font-size` even in the CSS mode.
  // Therefore, if the style is <font size="...">, we always set a <font>.
  const bool isSettingFontElement =
      IsStyleOfFontSize() ||
      (!aHTMLEditor.IsCSSEnabled() && IsStyleOfFontElement());
  Element* mostDistantEndParentHavingStyle = nullptr;
  for (Element* parent :
       endPoint.GetContainer()->InclusiveAncestorsOfType<Element>()) {
    if (!EditorUtils::IsEditableContent(*parent, EditorType::HTML) ||
        HTMLEditUtils::IsBlockElement(
            *parent, BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
        HTMLEditUtils::IsDisplayInsideFlowRoot(*parent)) {
      break;
    }
    if (ContentIsElementSettingTheStyle(aHTMLEditor, *parent)) {
      mostDistantEndParentHavingStyle = parent;
    }
    // If we're setting <font> element and there is a <font> element which is
    // entirely selected, we should use it.
    else if (isSettingFontElement && parent->IsHTMLElement(nsGkAtoms::font)) {
      mostDistantEndParentHavingStyle = parent;
    }
    if (parent->GetNextSibling()) {
      break;  // The parent is not last element in its parent, stop climbing.
    }
  }
  if (mostDistantEndParentHavingStyle) {
    endPoint.SetAfter(mostDistantEndParentHavingStyle);
  }
  return endPoint;
}

EditorRawDOMRange HTMLEditor::AutoInlineStyleSetter::
    GetExtendedRangeToMinimizeTheNumberOfNewElements(
        const HTMLEditor& aHTMLEditor, const nsINode& aCommonAncestor,
        EditorRawDOMPoint&& aStartPoint, EditorRawDOMPoint&& aEndPoint) const {
  MOZ_ASSERT(aStartPoint.IsSet());
  MOZ_ASSERT(aEndPoint.IsSet());

  // For minimizing the number of new elements, we should extend the range as
  // far as possible. E.g., `<span>[abc</span> <span>def]</span>` should be
  // styled as `<b><span>abc</span> <span>def</span></b>`.
  // Similarly, if the range crosses a block boundary, we should do same thing.
  // I.e., `<p><span>[abc</span></p><p><span>def]</span></p>` should become
  // `<p><b><span>abc</span></b></p><p><b><span>def</span></b></p>`.
  if (aStartPoint.GetContainer() != aEndPoint.GetContainer()) {
    while (aStartPoint.GetContainer() != &aCommonAncestor &&
           aStartPoint.IsInContentNode() && aStartPoint.GetContainerParent() &&
           aStartPoint.IsStartOfContainer()) {
      if (!EditorUtils::IsEditableContent(
              *aStartPoint.ContainerAs<nsIContent>(), EditorType::HTML) ||
          (aStartPoint.ContainerAs<nsIContent>()->IsElement() &&
           (HTMLEditUtils::IsBlockElement(
                *aStartPoint.ContainerAs<Element>(),
                BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
            HTMLEditUtils::IsDisplayInsideFlowRoot(
                *aStartPoint.ContainerAs<Element>())))) {
        break;
      }
      aStartPoint = aStartPoint.ParentPoint();
    }
    while (aEndPoint.GetContainer() != &aCommonAncestor &&
           aEndPoint.IsInContentNode() && aEndPoint.GetContainerParent() &&
           aEndPoint.IsEndOfContainer()) {
      if (!EditorUtils::IsEditableContent(*aEndPoint.ContainerAs<nsIContent>(),
                                          EditorType::HTML) ||
          (aEndPoint.ContainerAs<nsIContent>()->IsElement() &&
           (HTMLEditUtils::IsBlockElement(
                *aEndPoint.ContainerAs<Element>(),
                BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
            HTMLEditUtils::IsDisplayInsideFlowRoot(
                *aEndPoint.ContainerAs<Element>())))) {
        break;
      }
      aEndPoint.SetAfter(aEndPoint.ContainerAs<nsIContent>());
    }
  }

  // Additionally, if we'll set a CSS style, we want to wrap elements which
  // should have the new style into the range to avoid creating new <span>
  // element.
  if (!IsRepresentableWithHTML() ||
      (aHTMLEditor.IsCSSEnabled() && IsCSSSettable(*nsGkAtoms::span))) {
    // First, if pointing in a text node, use parent point.
    if (aStartPoint.IsInContentNode() && aStartPoint.IsStartOfContainer() &&
        aStartPoint.GetContainerParentAs<nsIContent>() &&
        EditorUtils::IsEditableContent(
            *aStartPoint.ContainerParentAs<nsIContent>(), EditorType::HTML) &&
        (!aStartPoint.GetContainerAs<Element>() ||
         !HTMLEditUtils::IsContainerNode(
             *aStartPoint.ContainerAs<nsIContent>())) &&
        EditorUtils::IsEditableContent(*aStartPoint.ContainerAs<nsIContent>(),
                                       EditorType::HTML)) {
      aStartPoint = aStartPoint.ParentPoint();
      MOZ_ASSERT(aStartPoint.IsSet());
    }
    if (aEndPoint.IsInContentNode() && aEndPoint.IsEndOfContainer() &&
        aEndPoint.GetContainerParentAs<nsIContent>() &&
        EditorUtils::IsEditableContent(
            *aEndPoint.ContainerParentAs<nsIContent>(), EditorType::HTML) &&
        (!aEndPoint.GetContainerAs<Element>() ||
         !HTMLEditUtils::IsContainerNode(
             *aEndPoint.ContainerAs<nsIContent>())) &&
        EditorUtils::IsEditableContent(*aEndPoint.ContainerAs<nsIContent>(),
                                       EditorType::HTML)) {
      aEndPoint.SetAfter(aEndPoint.GetContainer());
      MOZ_ASSERT(aEndPoint.IsSet());
    }
    // Then, wrap the container if it's a good element to set a CSS property.
    if (aStartPoint.IsInContentNode() && aStartPoint.GetContainerParent() &&
        // The point must be start of the container
        aStartPoint.IsStartOfContainer() &&
        // only if the pointing first child node cannot have `style` attribute
        (!aStartPoint.GetChildAs<nsStyledElement>() ||
         !ElementIsGoodContainerToSetStyle(
             *aStartPoint.ChildAs<nsStyledElement>())) &&
        // but don't cross block boundary at climbing up the tree
        !HTMLEditUtils::IsBlockElement(
            *aStartPoint.ContainerAs<nsIContent>(),
            BlockInlineCheck::UseComputedDisplayOutsideStyle) &&
        // and the container is a good editable element to set CSS style
        aStartPoint.GetContainerAs<nsStyledElement>() &&
        ElementIsGoodContainerToSetStyle(
            *aStartPoint.ContainerAs<nsStyledElement>())) {
      aStartPoint = aStartPoint.ParentPoint();
      MOZ_ASSERT(aStartPoint.IsSet());
    }
    if (aEndPoint.IsInContentNode() && aEndPoint.GetContainerParent() &&
        // The point must be end of the container
        aEndPoint.IsEndOfContainer() &&
        // only if the pointing last child node cannot have `style` attribute
        (aEndPoint.IsStartOfContainer() ||
         !aEndPoint.GetPreviousSiblingOfChildAs<nsStyledElement>() ||
         !ElementIsGoodContainerToSetStyle(
             *aEndPoint.GetPreviousSiblingOfChildAs<nsStyledElement>())) &&
        // but don't cross block boundary at climbing up the tree
        !HTMLEditUtils::IsBlockElement(
            *aEndPoint.ContainerAs<nsIContent>(),
            BlockInlineCheck::UseComputedDisplayOutsideStyle) &&
        // and the container is a good editable element to set CSS style
        aEndPoint.GetContainerAs<nsStyledElement>() &&
        ElementIsGoodContainerToSetStyle(
            *aEndPoint.ContainerAs<nsStyledElement>())) {
      aEndPoint.SetAfter(aEndPoint.GetContainer());
      MOZ_ASSERT(aEndPoint.IsSet());
    }
  }

  return EditorRawDOMRange(std::move(aStartPoint), std::move(aEndPoint));
}

Result<EditorRawDOMRange, nsresult>
HTMLEditor::AutoInlineStyleSetter::ExtendOrShrinkRangeToApplyTheStyle(
    const HTMLEditor& aHTMLEditor, const EditorDOMRange& aRange,
    const Element& aEditingHost) const {
  if (NS_WARN_IF(!aRange.IsPositioned())) {
    return Err(NS_ERROR_FAILURE);
  }

  // For avoiding assertion hits in the utility methods, check whether the
  // range is in same subtree, first. Even if the range crosses a subtree
  // boundary, it's not a bug of this module.
  nsINode* commonAncestor = aRange.GetClosestCommonInclusiveAncestor();
  if (NS_WARN_IF(!commonAncestor)) {
    return Err(NS_ERROR_FAILURE);
  }

  // If the range does not select only invisible <br> element, let's extend the
  // range to contain the <br> element.
  EditorDOMRange range(aRange);
  if (range.EndRef().IsInContentNode()) {
    const WSScanResult nextThing =
        HTMLEditUtils::ScanInclusiveNextThingWithIgnoringUnnecessaryLineBreak(
            range.EndRef(),
            // We don't want to wrap only the padding line break into the new
            // styled element.
            TreatAsUnnecessaryLineBreak{LineBreakFor::PaddingForEmptyBlock},
            aEditingHost);
    if (nextThing.MaybeIgnoredLineBreak().isSome() &&
        nextThing.MaybeIgnoredLineBreak()->IsInclusiveDescendantOf(
            aEditingHost) &&
        nextThing.MaybeIgnoredLineBreak()->ContentRef().GetParentElement() &&
        HTMLEditUtils::IsInlineContent(
            *nextThing.MaybeIgnoredLineBreak()->ContentRef().GetParentElement(),
            BlockInlineCheck::UseComputedDisplayOutsideStyle)) {
      range.SetEnd(
          nextThing.MaybeIgnoredLineBreak()->After<EditorRawDOMPoint>());
      MOZ_ASSERT(range.EndRef().IsSet());
      commonAncestor = range.GetClosestCommonInclusiveAncestor();
      if (NS_WARN_IF(!commonAncestor)) {
        return Err(NS_ERROR_FAILURE);
      }
    }
  }

  // If the range is collapsed, we don't want to replace ancestors unless it's
  // in an empty element.
  if (range.Collapsed() && range.StartRef().GetContainer()->Length()) {
    return EditorRawDOMRange(range);
  }

  // First, shrink the given range to minimize new style applied contents.
  // However, we should not shrink the range into entirely selected element.
  // E.g., if `abc[<i>def</i>]ghi`, shouldn't shrink it as
  // `abc<i>[def]</i>ghi`.
  EditorRawDOMPoint startPoint, endPoint;
  if (range.Collapsed()) {
    startPoint = endPoint = range.StartRef().To<EditorRawDOMPoint>();
  } else {
    ContentSubtreeIterator iter;
    if (NS_FAILED(iter.Init(range.StartRef().ToRawRangeBoundary(),
                            range.EndRef().ToRawRangeBoundary()))) {
      NS_WARNING("ContentSubtreeIterator::Init() failed");
      return Err(NS_ERROR_FAILURE);
    }
    nsIContent* const firstContentEntirelyInRange =
        nsIContent::FromNodeOrNull(iter.GetCurrentNode());
    nsIContent* const lastContentEntirelyInRange = [&]() {
      iter.Last();
      return nsIContent::FromNodeOrNull(iter.GetCurrentNode());
    }();

    // Compute the shrunken range boundaries.
    startPoint = GetShrunkenRangeStart(aHTMLEditor, range, *commonAncestor,
                                       firstContentEntirelyInRange);
    MOZ_ASSERT(startPoint.IsSet());
    endPoint = GetShrunkenRangeEnd(aHTMLEditor, range, *commonAncestor,
                                   lastContentEntirelyInRange);
    MOZ_ASSERT(endPoint.IsSet());

    // If shrunken range is swapped, it could like this case:
    // `abc[</span><span>]def`, starts at very end of a node and ends at
    // very start of immediately next node.  In this case, we should use
    // the original range instead.
    if (MOZ_UNLIKELY(!startPoint.EqualsOrIsBefore(endPoint))) {
      startPoint = range.StartRef().To<EditorRawDOMPoint>();
      endPoint = range.EndRef().To<EditorRawDOMPoint>();
    }
  }

  // Then, we may need to extend the range to wrap parent inline elements
  // which specify same style since we need to remove same style elements to
  // apply new value.  E.g., abc
  //   <span style="background-color: red">
  //     <span style="background-color: blue">[def]</span>
  //   </span>
  // ghi
  // In this case, we need to wrap the other <span> element if setting
  // background color.  Then, the inner <span> element is removed and the
  // other <span> element's style attribute will be updated rather than
  // inserting new <span> element.
  startPoint = GetExtendedRangeStartToWrapAncestorApplyingSameStyle(aHTMLEditor,
                                                                    startPoint);
  MOZ_ASSERT(startPoint.IsSet());
  endPoint =
      GetExtendedRangeEndToWrapAncestorApplyingSameStyle(aHTMLEditor, endPoint);
  MOZ_ASSERT(endPoint.IsSet());

  // Finally, we need to extend the range unless the range is in an element to
  // reduce the number of creating new elements.  E.g., if now selects
  // `<span>[abc</span><span>def]</span>`, we should make it
  // `<b><span>abc</span><span>def</span></b>` rather than
  // `<span><b>abc</b></span><span><b>def</b></span>`.
  EditorRawDOMRange finalRange =
      GetExtendedRangeToMinimizeTheNumberOfNewElements(
          aHTMLEditor, *commonAncestor, std::move(startPoint),
          std::move(endPoint));
#if 0
  fprintf(stderr,
          "ExtendOrShrinkRangeToApplyTheStyle:\n"
          "  Result: {(\n    %s\n  ) - (\n    %s\n  )},\n"
          "  Input: {(\n    %s\n  ) - (\n    %s\n  )}\n",
          ToString(finalRange.StartRef()).c_str(),
          ToString(finalRange.EndRef()).c_str(),
          ToString(aRange.StartRef()).c_str(),
          ToString(aRange.EndRef()).c_str());
#endif
  return finalRange;
}

Result<SplitRangeOffResult, nsresult>
HTMLEditor::SplitAncestorStyledInlineElementsAtRangeEdges(
    const EditorDOMRange& aRange, const EditorInlineStyle& aStyle,
    SplitAtEdges aSplitAtEdges) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aRange.IsPositioned())) {
    return Err(NS_ERROR_FAILURE);
  }

  EditorDOMRange range(aRange);

  // split any matching style nodes above the start of range
  auto resultAtStart =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<SplitNodeResult, nsresult> {
    AutoTrackDOMRange tracker(RangeUpdaterRef(), &range);
    Result<SplitNodeResult, nsresult> result =
        SplitAncestorStyledInlineElementsAt(range.StartRef(), aStyle,
                                            aSplitAtEdges);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::SplitAncestorStyledInlineElementsAt() failed");
      return result;
    }
    tracker.Flush(StopTracking::Yes);
    if (result.inspect().Handled()) {
      auto startOfRange = result.inspect().AtSplitPoint<EditorDOMPoint>();
      if (!startOfRange.IsSet()) {
        result.inspect().IgnoreCaretPointSuggestion();
        NS_WARNING(
            "HTMLEditor::SplitAncestorStyledInlineElementsAt() didn't return "
            "split point");
        return Err(NS_ERROR_FAILURE);
      }
      range.SetStart(std::move(startOfRange));
    } else if (MOZ_UNLIKELY(!range.IsPositioned())) {
      NS_WARNING(
          "HTMLEditor::SplitAncestorStyledInlineElementsAt() caused unexpected "
          "DOM tree");
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return result;
  }();
  if (MOZ_UNLIKELY(resultAtStart.isErr())) {
    return resultAtStart.propagateErr();
  }
  SplitNodeResult unwrappedResultAtStart = resultAtStart.unwrap();

  // second verse, same as the first...
  auto resultAtEnd =
      [&]() MOZ_CAN_RUN_SCRIPT -> Result<SplitNodeResult, nsresult> {
    AutoTrackDOMRange tracker(RangeUpdaterRef(), &range);
    Result<SplitNodeResult, nsresult> result =
        SplitAncestorStyledInlineElementsAt(range.EndRef(), aStyle,
                                            aSplitAtEdges);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::SplitAncestorStyledInlineElementsAt() failed");
      return result;
    }
    tracker.Flush(StopTracking::Yes);
    if (result.inspect().Handled()) {
      auto endOfRange = result.inspect().AtSplitPoint<EditorDOMPoint>();
      if (!endOfRange.IsSet()) {
        result.inspect().IgnoreCaretPointSuggestion();
        NS_WARNING(
            "HTMLEditor::SplitAncestorStyledInlineElementsAt() didn't return "
            "split point");
        return Err(NS_ERROR_FAILURE);
      }
      range.SetEnd(std::move(endOfRange));
    } else if (MOZ_UNLIKELY(!range.IsPositioned())) {
      NS_WARNING(
          "HTMLEditor::SplitAncestorStyledInlineElementsAt() caused unexpected "
          "DOM tree");
      return Err(NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE);
    }
    return result;
  }();
  if (MOZ_UNLIKELY(resultAtEnd.isErr())) {
    unwrappedResultAtStart.IgnoreCaretPointSuggestion();
    return resultAtEnd.propagateErr();
  }

  return SplitRangeOffResult(std::move(range),
                             std::move(unwrappedResultAtStart),
                             resultAtEnd.unwrap());
}

Result<SplitNodeResult, nsresult>
HTMLEditor::SplitAncestorStyledInlineElementsAt(
    const EditorDOMPoint& aPointToSplit, const EditorInlineStyle& aStyle,
    SplitAtEdges aSplitAtEdges) {
  // If the point is in a non-content node, e.g., in the document node, we
  // should split nothing.
  if (MOZ_UNLIKELY(!aPointToSplit.IsInContentNode())) {
    return SplitNodeResult::NotHandled(aPointToSplit);
  }

  // We assume that this method is called only when we're removing style(s).
  // Even if we're in HTML mode and there is no presentation element in the
  // block, we may need to overwrite the block's style with `<span>` element
  // and CSS.  For example, `<h1>` element has `font-weight: bold;` as its
  // default style.  If `Document.execCommand("bold")` is called for its
  // text, we should make it unbold.  Therefore, we shouldn't check
  // IsCSSEnabled() in most cases.  However, there is an exception.
  // FontFaceStateCommand::SetState() calls RemoveInlinePropertyAsAction()
  // with nsGkAtoms::tt before calling SetInlinePropertyAsAction() if we
  // are handling a XUL command.  Only in that case, we need to check
  // IsCSSEnabled().
  const bool handleCSS =
      aStyle.mHTMLProperty != nsGkAtoms::tt || IsCSSEnabled();

  AutoTArray<OwningNonNull<Element>, 24> arrayOfParents;
  for (Element* element :
       aPointToSplit.GetContainer()->InclusiveAncestorsOfType<Element>()) {
    if (element->IsAnyOfHTMLElements(nsGkAtoms::body, nsGkAtoms::head,
                                     nsGkAtoms::html) ||
        HTMLEditUtils::IsBlockElement(
            *element, BlockInlineCheck::UseComputedDisplayOutsideStyle) ||
        !element->GetParent() ||
        !EditorUtils::IsEditableContent(*element->GetParent(),
                                        EditorType::HTML) ||
        NS_WARN_IF(!HTMLEditUtils::IsSplittableNode(*element))) {
      break;
    }
    arrayOfParents.AppendElement(*element);
  }

  // Split any matching style nodes above the point.
  SplitNodeResult result = SplitNodeResult::NotHandled(aPointToSplit);
  MOZ_ASSERT(!result.Handled());
  EditorDOMPoint pointToPutCaret;
  for (const OwningNonNull<Element>& element : arrayOfParents) {
    auto isSetByCSSOrError = [&]() -> Result<bool, nsresult> {
      if (!handleCSS) {
        return false;
      }
      // The HTML style defined by aStyle has a CSS equivalence in this
      // implementation for the node; let's check if it carries those CSS
      // styles
      if (aStyle.IsCSSRemovable(*element)) {
        nsAutoString firstValue;
        Result<bool, nsresult> isSpecifiedByCSSOrError =
            CSSEditUtils::IsSpecifiedCSSEquivalentTo(*this, *element, aStyle,
                                                     firstValue);
        if (MOZ_UNLIKELY(isSpecifiedByCSSOrError.isErr())) {
          result.IgnoreCaretPointSuggestion();
          NS_WARNING("CSSEditUtils::IsSpecifiedCSSEquivalentTo() failed");
          return isSpecifiedByCSSOrError;
        }
        if (isSpecifiedByCSSOrError.unwrap()) {
          return true;
        }
      }
      // If this is <sub> or <sup>, we won't use vertical-align CSS property
      // because <sub>/<sup> changes font size but neither `vertical-align:
      // sub` nor `vertical-align: super` changes it (bug 394304 comment 2).
      // Therefore, they are not equivalents.  However, they're obviously
      // conflict with vertical-align style.  Thus, we need to remove ancestor
      // elements having vertical-align style.
      if (aStyle.IsStyleConflictingWithVerticalAlign()) {
        nsAutoString value;
        nsresult rv = CSSEditUtils::GetSpecifiedProperty(
            *element, *nsGkAtoms::vertical_align, value);
        if (NS_FAILED(rv)) {
          NS_WARNING("CSSEditUtils::GetSpecifiedProperty() failed");
          result.IgnoreCaretPointSuggestion();
          return Err(rv);
        }
        if (!value.IsEmpty()) {
          return true;
        }
      }
      return false;
    }();
    if (MOZ_UNLIKELY(isSetByCSSOrError.isErr())) {
      return isSetByCSSOrError.propagateErr();
    }
    if (!isSetByCSSOrError.inspect()) {
      if (!aStyle.IsStyleToClearAllInlineStyles()) {
        // If we're removing a link style and the element is an <a href>, we
        // need to split it.
        if (aStyle.mHTMLProperty == nsGkAtoms::href &&
            HTMLEditUtils::IsHyperlinkElement(element)) {
        }
        // If we're removing HTML style, we should split only the element
        // which represents the style.
        else if (!element->IsHTMLElement(aStyle.mHTMLProperty) ||
                 (aStyle.mAttribute && !element->HasAttr(aStyle.mAttribute))) {
          continue;
        }
        // If we're setting <font> related styles, it means that we're not
        // toggling the style.  In this case, we need to remove parent <font>
        // elements and/or update parent <font> elements if there are some
        // elements which have the attribute.  However, we should not touch if
        // the value is same as what the caller setting to keep the DOM tree
        // as-is as far as possible.
        if (aStyle.IsStyleOfFontElement() && aStyle.MaybeHasValue()) {
          const nsAttrValue* const attrValue =
              element->GetParsedAttr(aStyle.mAttribute);
          if (attrValue) {
            if (aStyle.mAttribute == nsGkAtoms::size) {
              if (attrValue->Type() == nsAttrValue::eInteger &&
                  nsContentUtils::ParseLegacyFontSize(
                      aStyle.AsInlineStyleAndValue().mAttributeValue) ==
                      attrValue->GetIntegerValue()) {
                continue;
              }
            } else if (aStyle.mAttribute == nsGkAtoms::color) {
              nsAttrValue newValue;
              nscolor oldColor, newColor;
              if (attrValue->Type() == nsAttrValue::eColor &&
                  attrValue->GetColorValue(oldColor) &&
                  newValue.ParseColor(
                      aStyle.AsInlineStyleAndValue().mAttributeValue) &&
                  newValue.GetColorValue(newColor) && oldColor == newColor) {
                continue;
              }
            } else if (attrValue->Equals(
                           aStyle.AsInlineStyleAndValue().mAttributeValue,
                           eIgnoreCase)) {
              continue;
            }
          }
        }
      }
      // If aProperty is nullptr, we need to split any style.
      else if (!EditorUtils::IsEditableContent(element, EditorType::HTML) ||
               !HTMLEditUtils::IsRemovableInlineStyleElement(*element)) {
        continue;
      }
    }

    // Found a style node we need to split.
    // XXX If first content is a text node and CSS is enabled, we call this
    //     with text node but in such case, this does nothing, but returns
    //     as handled with setting only previous or next node.  If its parent
    //     is a block, we do nothing but return as handled.
    AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(), &pointToPutCaret);
    Result<SplitNodeResult, nsresult> splitNodeResult =
        SplitNodeDeepWithTransaction(MOZ_KnownLive(element),
                                     result.AtSplitPoint<EditorDOMPoint>(),
                                     aSplitAtEdges);
    if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::SplitNodeDeepWithTransaction() failed");
      return splitNodeResult;
    }
    SplitNodeResult unwrappedSplitNodeResult = splitNodeResult.unwrap();
    trackPointToPutCaret.Flush(StopTracking::Yes);
    unwrappedSplitNodeResult.MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});

    // If it's not handled, it means that `content` is not a splittable node
    // like a void element even if it has some children, and the split point
    // is middle of it.
    if (!unwrappedSplitNodeResult.Handled()) {
      continue;
    }
    // Respect the last split result which actually did it.
    if (!result.DidSplit() || unwrappedSplitNodeResult.DidSplit()) {
      result = unwrappedSplitNodeResult.ToHandledResult();
    }
    MOZ_ASSERT(result.Handled());
  }

  return pointToPutCaret.IsSet()
             ? SplitNodeResult(std::move(result), std::move(pointToPutCaret))
             : std::move(result);
}

Result<EditorDOMPoint, nsresult> HTMLEditor::ClearStyleAt(
    const EditorDOMPoint& aPoint, const EditorInlineStyle& aStyleToRemove,
    SpecifiedStyle aSpecifiedStyle, const Element& aEditingHost) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!aPoint.IsSet())) {
    return Err(NS_ERROR_INVALID_ARG);
  }

  // TODO: We should rewrite this to stop unnecessary element creation and
  //       deleting it later because it causes the original element may be
  //       removed from the DOM tree even if same element is still in the
  //       DOM tree from point of view of users.

  // First, split inline elements at the point.
  // E.g., if aStyleToRemove.mHTMLProperty is nsGkAtoms::b and
  //       `<p><b><i>a[]bc</i></b></p>`, we want to make it as
  //       `<p><b><i>a</i></b><b><i>bc</i></b></p>`.
  EditorDOMPoint pointToPutCaret(aPoint);
  AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(), &pointToPutCaret);
  Result<SplitNodeResult, nsresult> splitNodeResult =
      SplitAncestorStyledInlineElementsAt(
          aPoint, aStyleToRemove, SplitAtEdges::eAllowToCreateEmptyContainer);
  if (MOZ_UNLIKELY(splitNodeResult.isErr())) {
    NS_WARNING("HTMLEditor::SplitAncestorStyledInlineElementsAt() failed");
    return splitNodeResult.propagateErr();
  }
  trackPointToPutCaret.Flush(StopTracking::Yes);
  SplitNodeResult unwrappedSplitNodeResult = splitNodeResult.unwrap();
  unwrappedSplitNodeResult.MoveCaretPointTo(
      pointToPutCaret, *this,
      {SuggestCaret::OnlyIfHasSuggestion,
       SuggestCaret::OnlyIfTransactionsAllowedToDoIt});

  // If there is no styled inline elements of aStyleToRemove, we just return the
  // given point.
  // E.g., `<p><i>a[]bc</i></p>` for nsGkAtoms::b.
  if (!unwrappedSplitNodeResult.Handled()) {
    return pointToPutCaret;
  }

  // If it did split nodes, but topmost ancestor inline element is split
  // at start of it, we don't need the empty inline element.  Let's remove
  // it now.  Then, we'll get the following DOM tree if there is no "a" in the
  // above case:
  // <p><b><i>bc</i></b></p>
  //   ^^
  if (unwrappedSplitNodeResult.GetPreviousContent() &&
      HTMLEditUtils::IsEmptyNode(
          *unwrappedSplitNodeResult.GetPreviousContent(),
          {EmptyCheckOption::TreatSingleBRElementAsVisible,
           EmptyCheckOption::TreatListItemAsVisible,
           EmptyCheckOption::TreatTableCellAsVisible})) {
    AutoTrackDOMPoint trackPointToPutCaret(RangeUpdaterRef(), &pointToPutCaret);
    // Delete previous node if it's empty.
    // MOZ_KnownLive(unwrappedSplitNodeResult.GetPreviousContent()):
    // It's grabbed by unwrappedSplitNodeResult.
    nsresult rv = DeleteNodeWithTransaction(
        MOZ_KnownLive(*unwrappedSplitNodeResult.GetPreviousContent()));
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
      return Err(rv);
    }
  }

  // If we reached block from end of a text node, we can do nothing here.
  // E.g., `<p style="font-weight: bold;">a[]bc</p>` for nsGkAtoms::b and
  // we're in CSS mode.
  // XXX Chrome resets block style and creates `<span>` elements for each
  //     line in this case.
  if (!unwrappedSplitNodeResult.GetNextContent()) {
    return pointToPutCaret;
  }

  // Otherwise, the next node is topmost ancestor inline element which has
  // the style.  We want to put caret between the split nodes, but we need
  // to keep other styles.  Therefore, next, we need to split at start of
  // the next node.  The first example should become
  // `<p><b><i>a</i></b><b><i></i></b><b><i>bc</i></b></p>`.
  //                    ^^^^^^^^^^^^^^
  nsIContent* firstLeafChildOfNextNode = HTMLEditUtils::GetFirstLeafContent(
      *unwrappedSplitNodeResult.GetNextContent(), {});
  EditorDOMPoint atStartOfNextNode(
      firstLeafChildOfNextNode ? firstLeafChildOfNextNode
                               : unwrappedSplitNodeResult.GetNextContent(),
      0);
  Maybe<EditorLineBreak> lineBreak;
  // But don't try to split non-containers like `<br>`, `<hr>` and `<img>`
  // element.
  if (!atStartOfNextNode.IsInContentNode() ||
      !HTMLEditUtils::IsContainerNode(
          *atStartOfNextNode.ContainerAs<nsIContent>())) {
    // If it's a `<br>` element, let's move it into new node later.
    auto* const brElement =
        HTMLBRElement::FromNode(atStartOfNextNode.GetContainer());
    if (brElement) {
      lineBreak.emplace(*brElement);
    }
    if (!atStartOfNextNode.GetContainerParentAs<nsIContent>()) {
      NS_WARNING("atStartOfNextNode was in an orphan node");
      return Err(NS_ERROR_FAILURE);
    }
    atStartOfNextNode.Set(atStartOfNextNode.GetContainerParent(), 0);
  }
  AutoTrackDOMPoint trackPointToPutCaret2(RangeUpdaterRef(), &pointToPutCaret);
  Result<SplitNodeResult, nsresult> splitResultAtStartOfNextNode =
      SplitAncestorStyledInlineElementsAt(
          atStartOfNextNode, aStyleToRemove,
          SplitAtEdges::eAllowToCreateEmptyContainer);
  if (MOZ_UNLIKELY(splitResultAtStartOfNextNode.isErr())) {
    NS_WARNING("HTMLEditor::SplitAncestorStyledInlineElementsAt() failed");
    return splitResultAtStartOfNextNode.propagateErr();
  }
  trackPointToPutCaret2.Flush(StopTracking::Yes);
  SplitNodeResult unwrappedSplitResultAtStartOfNextNode =
      splitResultAtStartOfNextNode.unwrap();
  unwrappedSplitResultAtStartOfNextNode.MoveCaretPointTo(
      pointToPutCaret, *this,
      {SuggestCaret::OnlyIfHasSuggestion,
       SuggestCaret::OnlyIfTransactionsAllowedToDoIt});

  if (unwrappedSplitResultAtStartOfNextNode.Handled() &&
      unwrappedSplitResultAtStartOfNextNode.GetNextContent()) {
    // If the right inline elements are empty, we should remove them.  E.g.,
    // if the split point is at end of a text node (or end of an inline
    // element), e.g., <div><b><i>abc[]</i></b></div>, then now, it's been
    // changed to:
    // <div><b><i>abc</i></b><b><i>[]</i></b><b><i></i></b></div>
    //                                       ^^^^^^^^^^^^^^
    // We will change it to:
    // <div><b><i>abc</i></b><b><i>[]</i></b></div>
    //                                      ^^
    // And if it has only padding <br> element, we should move it into the
    // previous <i> which will have new content.
    bool seenBR = false;
    if (HTMLEditUtils::IsEmptyNode(
            *unwrappedSplitResultAtStartOfNextNode.GetNextContent(),
            {EmptyCheckOption::TreatListItemAsVisible,
             EmptyCheckOption::TreatTableCellAsVisible},
            &seenBR)) {
      // MOZ_KnownLive because of grabbed by
      // unwrappedSplitResultAtStartOfNextNode.
      nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(
          *unwrappedSplitResultAtStartOfNextNode.GetNextContent()));
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
        return Err(rv);
      }
    }
  }

  if (!unwrappedSplitResultAtStartOfNextNode.Handled()) {
    return std::move(pointToPutCaret);
  }

  // If there is no content, we should return here.
  // XXX Is this possible case without mutation event listener?
  if (!unwrappedSplitResultAtStartOfNextNode.GetPreviousContent()) {
    // XXX This is really odd, but we return this value...
    const auto splitPoint =
        unwrappedSplitNodeResult.AtSplitPoint<EditorRawDOMPoint>();
    const auto splitPointAtStartOfNextNode =
        unwrappedSplitResultAtStartOfNextNode.AtSplitPoint<EditorRawDOMPoint>();
    return EditorDOMPoint(splitPoint.GetContainer(),
                          splitPointAtStartOfNextNode.Offset());
  }

  // Now, we want to put `<br>` element into the empty split node if
  // it was in next node of the first split.
  // E.g., `<p><b><i>a</i></b><b><i><br></i></b><b><i>bc</i></b></p>`
  nsIContent* firstLeafChildOfPreviousNode = HTMLEditUtils::GetFirstLeafContent(
      *unwrappedSplitResultAtStartOfNextNode.GetPreviousContent(), {});
  pointToPutCaret.Set(
      firstLeafChildOfPreviousNode
          ? firstLeafChildOfPreviousNode
          : unwrappedSplitResultAtStartOfNextNode.GetPreviousContent(),
      0);

  // If the right node starts with a line break, suck it out of right node and
  // into the left node left node.  This is so we you don't revert back to the
  // previous style if you happen to click at the end of a line.
  if (lineBreak.isSome()) {
    if (lineBreak->IsInComposedDoc()) {
      Result<EditorDOMPoint, nsresult> lineBreakPointOrError =
          DeleteLineBreakWithTransaction(lineBreak.ref(), nsIEditor::eStrip,
                                         aEditingHost);
      if (MOZ_UNLIKELY(lineBreakPointOrError.isErr())) {
        NS_WARNING("HTMLEditor::DeleteLineBreakWithTransaction() failed");
        return lineBreakPointOrError.propagateErr();
      }
    }
    Result<CreateLineBreakResult, nsresult> insertBRElementResultOrError =
        InsertLineBreak(WithTransaction::Yes, LineBreakType::BRElement,
                        pointToPutCaret);
    if (MOZ_UNLIKELY(insertBRElementResultOrError.isErr())) {
      NS_WARNING(
          "HTMLEditor::InsertLineBreak(WithTransaction::Yes, "
          "LineBreakType::BRElement) failed");
      return insertBRElementResultOrError.propagateErr();
    }
    CreateLineBreakResult insertBRElementResult =
        insertBRElementResultOrError.unwrap();
    insertBRElementResult.MoveCaretPointTo(
        pointToPutCaret, *this,
        {SuggestCaret::OnlyIfHasSuggestion,
         SuggestCaret::OnlyIfTransactionsAllowedToDoIt});

    if (unwrappedSplitResultAtStartOfNextNode.GetNextContent() &&
        unwrappedSplitResultAtStartOfNextNode.GetNextContent()
            ->IsInComposedDoc()) {
      // If we split inline elements at immediately before <br> element which is
      // the last visible content in the right element, we don't need the right
      // element anymore.  Otherwise, we'll create the following DOM tree:
      // - <b>abc</b>{}<br><b></b>
      //                   ^^^^^^^
      // - <b><i>abc</i></b><i><br></i><b></b>
      //                               ^^^^^^^
      if (HTMLEditUtils::IsEmptyNode(
              *unwrappedSplitResultAtStartOfNextNode.GetNextContent(),
              {EmptyCheckOption::TreatSingleBRElementAsVisible,
               EmptyCheckOption::TreatListItemAsVisible,
               EmptyCheckOption::TreatTableCellAsVisible})) {
        // MOZ_KnownLive because the result is grabbed by
        // unwrappedSplitResultAtStartOfNextNode.
        nsresult rv = DeleteNodeWithTransaction(MOZ_KnownLive(
            *unwrappedSplitResultAtStartOfNextNode.GetNextContent()));
        if (NS_FAILED(rv)) {
          NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
          return Err(rv);
        }
      }
      // If the next content has only one <br> element, there may be empty
      // inline elements around it.  We don't need them anymore because user
      // cannot put caret into them.  E.g., <b><i>abc[]<br></i><br></b> has
      // been changed to <b><i>abc</i></b><i>{}<br></i><b><i></i><br></b> now.
      //                                               ^^^^^^^^^^^^^^^^^^
      // We don't need the empty <i>.
      else if (HTMLEditUtils::IsEmptyNode(
                   *unwrappedSplitResultAtStartOfNextNode.GetNextContent(),
                   {EmptyCheckOption::TreatListItemAsVisible,
                    EmptyCheckOption::TreatTableCellAsVisible})) {
        AutoTArray<OwningNonNull<nsIContent>, 4> emptyInlineContainerElements;
        HTMLEditUtils::CollectEmptyInlineContainerDescendants(
            *unwrappedSplitResultAtStartOfNextNode.GetNextContentAs<Element>(),
            emptyInlineContainerElements,
            {EmptyCheckOption::TreatSingleBRElementAsVisible,
             EmptyCheckOption::TreatListItemAsVisible,
             EmptyCheckOption::TreatTableCellAsVisible},
            BlockInlineCheck::UseComputedDisplayOutsideStyle);
        for (const OwningNonNull<nsIContent>& emptyInlineContainerElement :
             emptyInlineContainerElements) {
          // MOZ_KnownLive(emptyInlineContainerElement) due to bug 1622253.
          nsresult rv = DeleteNodeWithTransaction(
              MOZ_KnownLive(emptyInlineContainerElement));
          if (NS_FAILED(rv)) {
            NS_WARNING("EditorBase::DeleteNodeWithTransaction() failed");
            return Err(rv);
          }
        }
      }
    }

    // Update the child.
    pointToPutCaret.Set(pointToPutCaret.GetContainer(), 0);
  }
  // Finally, remove the specified style in the previous node at the
  // second split and tells good insertion point to the caller.  I.e., we
  // want to make the first example as:
  // `<p><b><i>a</i></b><i>[]</i><b><i>bc</i></b></p>`
  //                    ^^^^^^^^^
  if (auto* const previousElementOfSplitPoint =
          unwrappedSplitResultAtStartOfNextNode
              .GetPreviousContentAs<Element>()) {
    // Track the point at the new hierarchy.  This is so we can know where
    // to put the selection after we call RemoveStyleInside().
    // RemoveStyleInside() could remove any and all of those nodes, so I
    // have to use the range tracking system to find the right spot to put
    // selection.
    AutoTrackDOMPoint tracker(RangeUpdaterRef(), &pointToPutCaret);
    // MOZ_KnownLive(previousElementOfSplitPoint):
    // It's grabbed by unwrappedSplitResultAtStartOfNextNode.
    Result<EditorDOMPoint, nsresult> removeStyleResult =
        RemoveStyleInside(MOZ_KnownLive(*previousElementOfSplitPoint),
                          aStyleToRemove, aSpecifiedStyle);
    if (MOZ_UNLIKELY(removeStyleResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveStyleInside() failed");
      return removeStyleResult;
    }
    // We've already computed a suggested caret position at start of first leaf
    // which is stored in pointToPutCaret, so we don't need to update it here.
  }
  return pointToPutCaret;
}

Result<EditorDOMPoint, nsresult> HTMLEditor::RemoveStyleInside(
    Element& aElement, const EditorInlineStyle& aStyleToRemove,
    SpecifiedStyle aSpecifiedStyle) {
  // First, handle all descendants.
  AutoTArray<OwningNonNull<nsIContent>, 32> arrayOfChildContents;
  HTMLEditUtils::CollectAllChildren(aElement, arrayOfChildContents);
  EditorDOMPoint pointToPutCaret;
  for (const OwningNonNull<nsIContent>& child : arrayOfChildContents) {
    if (!child->IsElement()) {
      continue;
    }
    Result<EditorDOMPoint, nsresult> removeStyleResult = RemoveStyleInside(
        MOZ_KnownLive(*child->AsElement()), aStyleToRemove, aSpecifiedStyle);
    if (MOZ_UNLIKELY(removeStyleResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveStyleInside() failed");
      return removeStyleResult;
    }
    if (removeStyleResult.inspect().IsSet()) {
      pointToPutCaret = removeStyleResult.unwrap();
    }
  }

  // TODO: It seems that if aElement is not editable, we should insert new
  //       container to remove the style if possible.
  if (!EditorUtils::IsEditableContent(aElement, EditorType::HTML)) {
    return pointToPutCaret;
  }

  // Next, remove CSS style first.  Then, `style` attribute will be removed if
  // the corresponding CSS property is last one.
  auto isStyleSpecifiedOrError = [&]() -> Result<bool, nsresult> {
    if (!aStyleToRemove.IsCSSRemovable(aElement)) {
      return false;
    }
    MOZ_ASSERT(!aStyleToRemove.IsStyleToClearAllInlineStyles());
    Result<bool, nsresult> elementHasSpecifiedCSSEquivalentStylesOrError =
        CSSEditUtils::HaveSpecifiedCSSEquivalentStyles(*this, aElement,
                                                       aStyleToRemove);
    NS_WARNING_ASSERTION(
        elementHasSpecifiedCSSEquivalentStylesOrError.isOk(),
        "CSSEditUtils::HaveSpecifiedCSSEquivalentStyles() failed");
    return elementHasSpecifiedCSSEquivalentStylesOrError;
  }();
  if (MOZ_UNLIKELY(isStyleSpecifiedOrError.isErr())) {
    return isStyleSpecifiedOrError.propagateErr();
  }
  bool styleSpecified = isStyleSpecifiedOrError.unwrap();
  if (nsStyledElement* styledElement = nsStyledElement::FromNode(&aElement)) {
    if (styleSpecified) {
      // MOZ_KnownLive(*styledElement) because it's an alias of aElement.
      nsresult rv = CSSEditUtils::RemoveCSSEquivalentToStyle(
          WithTransaction::Yes, *this, MOZ_KnownLive(*styledElement),
          aStyleToRemove, nullptr);
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "CSSEditUtils::RemoveCSSEquivalentToStyle() failed, but ignored");
    }

    // If the style is <sub> or <sup>, we won't use vertical-align CSS
    // property because <sub>/<sup> changes font size but neither
    // `vertical-align: sub` nor `vertical-align: super` changes it
    // (bug 394304 comment 2).  Therefore, they are not equivalents.  However,
    // they're obviously conflict with vertical-align style.  Thus, we need to
    // remove the vertical-align style from elements.
    if (aStyleToRemove.IsStyleConflictingWithVerticalAlign()) {
      nsAutoString value;
      nsresult rv = CSSEditUtils::GetSpecifiedProperty(
          aElement, *nsGkAtoms::vertical_align, value);
      if (NS_FAILED(rv)) {
        NS_WARNING("CSSEditUtils::GetSpecifiedProperty() failed");
        return Err(rv);
      }
      if (!value.IsEmpty()) {
        // MOZ_KnownLive(*styledElement) because it's an alias of aElement.
        nsresult rv = CSSEditUtils::RemoveCSSPropertyWithTransaction(
            *this, MOZ_KnownLive(*styledElement), *nsGkAtoms::vertical_align,
            value);
        if (NS_FAILED(rv)) {
          NS_WARNING("CSSEditUtils::RemoveCSSPropertyWithTransaction() failed");
          return Err(rv);
        }
        styleSpecified = true;
      }
    }
  }

  // Then, if we could and should remove or replace aElement, let's do it. Or
  // just remove attribute.
  const bool isStyleRepresentedByElement =
      !aStyleToRemove.IsStyleToClearAllInlineStyles() &&
      aStyleToRemove.IsRepresentedBy(aElement);

  auto ShouldUpdateDOMTree = [&]() {
    // If we're removing any inline styles and aElement is an inline style
    // element, we can remove or replace it.
    if (aStyleToRemove.IsStyleToClearAllInlineStyles() &&
        HTMLEditUtils::IsRemovableInlineStyleElement(aElement)) {
      return true;
    }
    // If we're a specific style and aElement represents it, we can remove or
    // replace the element or remove the corresponding attribute.
    if (isStyleRepresentedByElement) {
      return true;
    }
    // If we've removed a CSS style from the `style` attribute of aElement, we
    // could remove the element.
    return aElement.IsHTMLElement(nsGkAtoms::span) && styleSpecified;
  };
  if (!ShouldUpdateDOMTree()) {
    return pointToPutCaret;
  }

  const bool elementHasNecessaryAttributes = [&]() {
    // If we're not removing nor replacing aElement itself, we don't need to
    // take care of its `style` and `class` attributes even if aSpecifiedStyle
    // is `Discard` because aSpecifiedStyle is not intended to be used in this
    // case.
    if (!isStyleRepresentedByElement) {
      return HTMLEditUtils::ElementHasAttributeExcept(aElement,
                                                      *nsGkAtoms::_empty);
    }
    // If we're removing links, we don't need to keep <a> even if it has some
    // specific attributes because it cannot be nested.  However, if and only if
    // it has `style` attribute and aSpecifiedStyle is not `Discard`, we need to
    // replace it with new <span> to keep the style.
    if (aStyleToRemove.IsStyleOfAnchorElement()) {
      return aSpecifiedStyle == SpecifiedStyle::Preserve &&
             (aElement.HasNonEmptyAttr(nsGkAtoms::style) ||
              aElement.HasNonEmptyAttr(nsGkAtoms::_class));
    }
    nsAtom& attrKeepStaying = aStyleToRemove.mAttribute
                                  ? *aStyleToRemove.mAttribute
                                  : *nsGkAtoms::_empty;
    return aSpecifiedStyle == SpecifiedStyle::Preserve
               // If we're try to remove the element but the caller wants to
               // preserve the style, check whether aElement has attributes
               // except the removing attribute since `style` and `class` should
               // keep existing to preserve the style.
               ? HTMLEditUtils::ElementHasAttributeExcept(aElement,
                                                          attrKeepStaying)
               // If we're try to remove the element and the caller wants to
               // discard the style specified to the element, check whether
               // aElement has attributes except the removing attribute, `style`
               // and `class` since we don't want to keep these attributes.
               : HTMLEditUtils::ElementHasAttributeExcept(
                     aElement, attrKeepStaying, *nsGkAtoms::style,
                     *nsGkAtoms::_class);
  }();

  // If the element is not a <span> and still has some attributes, we should
  // replace it with new <span>.
  auto ReplaceWithNewSpan = [&]() {
    if (aStyleToRemove.IsStyleToClearAllInlineStyles()) {
      return false;  // Remove it even if it has attributes.
    }
    if (aElement.IsHTMLElement(nsGkAtoms::span)) {
      return false;  // Don't replace <span> with new <span>.
    }
    if (!isStyleRepresentedByElement) {
      return false;  // Keep non-related element as-is.
    }
    if (!elementHasNecessaryAttributes) {
      return false;  // Should remove it instead of replacing it.
    }
    if (aElement.IsHTMLElement(nsGkAtoms::font)) {
      // Replace <font> if it won't have its specific attributes.
      return (aStyleToRemove.mHTMLProperty == nsGkAtoms::color ||
              !aElement.HasAttr(nsGkAtoms::color)) &&
             (aStyleToRemove.mHTMLProperty == nsGkAtoms::face ||
              !aElement.HasAttr(nsGkAtoms::face)) &&
             (aStyleToRemove.mHTMLProperty == nsGkAtoms::size ||
              !aElement.HasAttr(nsGkAtoms::size));
    }
    // The styled element has only global attributes, let's replace it with new
    // <span> with cloning the attributes.
    return true;
  };

  if (ReplaceWithNewSpan()) {
    // Before cloning the attribute to new element, let's remove it.
    if (aStyleToRemove.mAttribute) {
      nsresult rv =
          RemoveAttributeWithTransaction(aElement, *aStyleToRemove.mAttribute);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::RemoveAttributeWithTransaction() failed");
        return Err(rv);
      }
    }
    if (aSpecifiedStyle == SpecifiedStyle::Discard) {
      nsresult rv = RemoveAttributeWithTransaction(aElement, *nsGkAtoms::style);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "EditorBase::RemoveAttributeWithTransaction(nsGkAtoms::style) "
            "failed");
        return Err(rv);
      }
      rv = RemoveAttributeWithTransaction(aElement, *nsGkAtoms::_class);
      if (NS_FAILED(rv)) {
        NS_WARNING(
            "EditorBase::RemoveAttributeWithTransaction(nsGkAtoms::_class) "
            "failed");
        return Err(rv);
      }
    }
    // Move `style` attribute and `class` element to span element before
    // removing aElement from the tree.
    auto replaceWithSpanResult =
        [&]() MOZ_CAN_RUN_SCRIPT -> Result<CreateElementResult, nsresult> {
      if (!aStyleToRemove.IsStyleOfAnchorElement()) {
        return ReplaceContainerAndCloneAttributesWithTransaction(
            aElement, *nsGkAtoms::span);
      }
      nsString styleValue;  // Use nsString to avoid copying the buffer at
                            // setting the attribute.
      aElement.GetAttr(nsGkAtoms::style, styleValue);
      return ReplaceContainerWithTransaction(aElement, *nsGkAtoms::span,
                                             *nsGkAtoms::style, styleValue);
    }();
    if (MOZ_UNLIKELY(replaceWithSpanResult.isErr())) {
      NS_WARNING(
          "HTMLEditor::ReplaceContainerWithTransaction(nsGkAtoms::span) "
          "failed");
      return replaceWithSpanResult.propagateErr();
    }
    CreateElementResult unwrappedReplaceWithSpanResult =
        replaceWithSpanResult.unwrap();
    if (AllowsTransactionsToChangeSelection()) {
      unwrappedReplaceWithSpanResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    } else {
      unwrappedReplaceWithSpanResult.IgnoreCaretPointSuggestion();
    }
    return pointToPutCaret;
  }

  auto RemoveElement = [&]() {
    if (aStyleToRemove.IsStyleToClearAllInlineStyles()) {
      MOZ_ASSERT(HTMLEditUtils::IsRemovableInlineStyleElement(aElement));
      return true;
    }
    // If the element still has some attributes, we should not remove it to keep
    // current presentation and/or semantics.
    if (elementHasNecessaryAttributes) {
      return false;
    }
    // If the style is represented by the element, let's remove it.
    if (isStyleRepresentedByElement) {
      return true;
    }
    // If we've removed a CSS style and that made the <span> element have no
    // attributes, we can delete it.
    if (styleSpecified && aElement.IsHTMLElement(nsGkAtoms::span)) {
      return true;
    }
    return false;
  };

  if (RemoveElement()) {
    Result<EditorDOMPoint, nsresult> unwrapElementResult =
        RemoveContainerWithTransaction(aElement);
    if (MOZ_UNLIKELY(unwrapElementResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveContainerWithTransaction() failed");
      return unwrapElementResult.propagateErr();
    }
    if (AllowsTransactionsToChangeSelection() &&
        unwrapElementResult.inspect().IsSet()) {
      pointToPutCaret = unwrapElementResult.unwrap();
    }
    return pointToPutCaret;
  }

  // If the element needs to keep having some attributes, just remove the
  // attribute.  Note that we don't need to remove `style` attribute here when
  // aSpecifiedStyle is `Discard` because we've already removed unnecessary
  // CSS style above.
  if (isStyleRepresentedByElement && aStyleToRemove.mAttribute) {
    nsresult rv =
        RemoveAttributeWithTransaction(aElement, *aStyleToRemove.mAttribute);
    if (NS_FAILED(rv)) {
      NS_WARNING("EditorBase::RemoveAttributeWithTransaction() failed");
      return Err(rv);
    }
  }
  return pointToPutCaret;
}

EditorRawDOMRange HTMLEditor::GetExtendedRangeWrappingNamedAnchor(
    const EditorRawDOMRange& aRange) const {
  MOZ_ASSERT(aRange.StartRef().IsSet());
  MOZ_ASSERT(aRange.EndRef().IsSet());

  // FYI: We don't want to stop at ancestor block boundaries to extend the range
  // because <a name> can have block elements with low level DOM API.  We want
  // to remove any <a name> ancestors to remove the style.

  EditorRawDOMRange newRange(aRange);
  for (Element* element :
       aRange.StartRef().GetContainer()->InclusiveAncestorsOfType<Element>()) {
    if (!HTMLEditUtils::IsNamedAnchorElement(*element)) {
      continue;
    }
    newRange.SetStart(EditorRawDOMPoint(element));
  }
  for (Element* element :
       aRange.EndRef().GetContainer()->InclusiveAncestorsOfType<Element>()) {
    if (!HTMLEditUtils::IsNamedAnchorElement(*element)) {
      continue;
    }
    newRange.SetEnd(EditorRawDOMPoint::After(*element));
  }
  return newRange;
}

EditorRawDOMRange HTMLEditor::GetExtendedRangeWrappingEntirelySelectedElements(
    const EditorRawDOMRange& aRange) const {
  MOZ_ASSERT(aRange.StartRef().IsSet());
  MOZ_ASSERT(aRange.EndRef().IsSet());

  // FYI: We don't want to stop at ancestor block boundaries to extend the range
  // because the style may come from inline parents of block elements which may
  // occur in invalid DOM tree.  We want to split any (even invalid) ancestors
  // at removing the styles.

  EditorRawDOMRange newRange(aRange);
  while (newRange.StartRef().IsInContentNode() &&
         newRange.StartRef().IsStartOfContainer()) {
    if (!EditorUtils::IsEditableContent(
            *newRange.StartRef().ContainerAs<nsIContent>(), EditorType::HTML)) {
      break;
    }
    newRange.SetStart(newRange.StartRef().ParentPoint());
  }
  while (newRange.EndRef().IsInContentNode() &&
         newRange.EndRef().IsEndOfContainer()) {
    if (!EditorUtils::IsEditableContent(
            *newRange.EndRef().ContainerAs<nsIContent>(), EditorType::HTML)) {
      break;
    }
    newRange.SetEnd(
        EditorRawDOMPoint::After(*newRange.EndRef().ContainerAs<nsIContent>()));
  }
  return newRange;
}

nsresult HTMLEditor::GetInlinePropertyBase(const EditorInlineStyle& aStyle,
                                           const nsAString* aValue,
                                           bool* aFirst, bool* aAny, bool* aAll,
                                           nsAString* outValue) const {
  MOZ_ASSERT(!aStyle.IsStyleToClearAllInlineStyles());
  MOZ_ASSERT(IsEditActionDataAvailable());

  *aAny = false;
  *aAll = true;
  *aFirst = false;
  bool first = true;

  const bool isCollapsed = SelectionRef().IsCollapsed();
  RefPtr<dom::Range> range = SelectionRef().GetRangeAt(0);
  // XXX: Should be a while loop, to get each separate range
  // XXX: ERROR_HANDLING can currentItem be null?
  if (range) {
    // For each range, set a flag
    bool firstNodeInRange = true;

    if (isCollapsed) {
      if (NS_WARN_IF(!range->GetStartContainer())) {
        return NS_ERROR_FAILURE;
      }
      nsString tOutString;
      const PendingStyleState styleState = [&]() {
        if (aStyle.mAttribute) {
          auto state = mPendingStylesToApplyToNewContent->GetStyleState(
              *aStyle.mHTMLProperty, aStyle.mAttribute, &tOutString);
          if (outValue) {
            outValue->Assign(tOutString);
          }
          return state;
        }
        return mPendingStylesToApplyToNewContent->GetStyleState(
            *aStyle.mHTMLProperty);
      }();
      if (styleState != PendingStyleState::NotUpdated) {
        *aFirst = *aAny = *aAll =
            (styleState == PendingStyleState::BeingPreserved);
        return NS_OK;
      }

      nsIContent* const collapsedContent =
          nsIContent::FromNode(range->GetStartContainer());
      if (MOZ_LIKELY(collapsedContent &&
                     collapsedContent->GetAsElementOrParentElement()) &&
          aStyle.IsCSSSettable(
              *collapsedContent->GetAsElementOrParentElement())) {
        if (aValue) {
          tOutString.Assign(*aValue);
        }
        Result<bool, nsresult> isComputedCSSEquivalentToStyleOrError =
            CSSEditUtils::IsComputedCSSEquivalentTo(
                *this, MOZ_KnownLive(*collapsedContent), aStyle, tOutString);
        if (MOZ_UNLIKELY(isComputedCSSEquivalentToStyleOrError.isErr())) {
          NS_WARNING("CSSEditUtils::IsComputedCSSEquivalentTo() failed");
          return isComputedCSSEquivalentToStyleOrError.unwrapErr();
        }
        *aFirst = *aAny = *aAll =
            isComputedCSSEquivalentToStyleOrError.unwrap();
        if (outValue) {
          outValue->Assign(tOutString);
        }
        return NS_OK;
      }

      *aFirst = *aAny = *aAll =
          collapsedContent && HTMLEditUtils::IsInlineStyleSetByElement(
                                  *collapsedContent, aStyle, aValue, outValue);
      return NS_OK;
    }

    // Non-collapsed selection

    nsAutoString firstValue, theValue;

    nsCOMPtr<nsINode> endNode = range->GetEndContainer();
    uint32_t endOffset = range->EndOffset();

    PostContentIterator postOrderIter;
    DebugOnly<nsresult> rvIgnored = postOrderIter.Init(range);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                         "Failed to initialize post-order content iterator");
    for (; !postOrderIter.IsDone(); postOrderIter.Next()) {
      if (postOrderIter.GetCurrentNode()->IsHTMLElement(nsGkAtoms::body)) {
        break;
      }
      RefPtr<Text> textNode = Text::FromNode(postOrderIter.GetCurrentNode());
      if (!textNode) {
        continue;
      }

      // just ignore any non-editable nodes
      if (!EditorUtils::IsEditableContent(*textNode, EditorType::HTML) ||
          !HTMLEditUtils::IsVisibleTextNode(
              *textNode, TreatInvisibleLineBreakAs::Visible)) {
        continue;
      }

      if (!isCollapsed && first && firstNodeInRange) {
        firstNodeInRange = false;
        if (range->StartOffset() == textNode->TextDataLength()) {
          continue;
        }
      } else if (textNode == endNode && !endOffset) {
        continue;
      }

      const RefPtr<Element> element = textNode->GetParentElement();

      bool isSet = false;
      if (first) {
        if (element) {
          if (aStyle.IsCSSSettable(*element)) {
            // The HTML styles defined by aHTMLProperty/aAttribute have a CSS
            // equivalence in this implementation for node; let's check if it
            // carries those CSS styles
            if (aValue) {
              firstValue.Assign(*aValue);
            }
            Result<bool, nsresult> isComputedCSSEquivalentToStyleOrError =
                CSSEditUtils::IsComputedCSSEquivalentTo(*this, *element, aStyle,
                                                        firstValue);
            if (MOZ_UNLIKELY(isComputedCSSEquivalentToStyleOrError.isErr())) {
              NS_WARNING("CSSEditUtils::IsComputedCSSEquivalentTo() failed");
              return isComputedCSSEquivalentToStyleOrError.unwrapErr();
            }
            isSet = isComputedCSSEquivalentToStyleOrError.unwrap();
          } else {
            isSet = HTMLEditUtils::IsInlineStyleSetByElement(
                *element, aStyle, aValue, &firstValue);
          }
        }
        *aFirst = isSet;
        first = false;
        if (outValue) {
          *outValue = firstValue;
        }
      } else {
        if (element) {
          if (aStyle.IsCSSSettable(*element)) {
            // The HTML styles defined by aHTMLProperty/aAttribute have a CSS
            // equivalence in this implementation for node; let's check if it
            // carries those CSS styles
            if (aValue) {
              theValue.Assign(*aValue);
            }
            Result<bool, nsresult> isComputedCSSEquivalentToStyleOrError =
                CSSEditUtils::IsComputedCSSEquivalentTo(*this, *element, aStyle,
                                                        theValue);
            if (MOZ_UNLIKELY(isComputedCSSEquivalentToStyleOrError.isErr())) {
              NS_WARNING("CSSEditUtils::IsComputedCSSEquivalentTo() failed");
              return isComputedCSSEquivalentToStyleOrError.unwrapErr();
            }
            isSet = isComputedCSSEquivalentToStyleOrError.unwrap();
          } else {
            isSet = HTMLEditUtils::IsInlineStyleSetByElement(*element, aStyle,
                                                             aValue, &theValue);
          }
        }

        if (firstValue != theValue &&
            // For text-decoration related HTML properties, i.e. <u> and
            // <strike>, we have to also check |isSet| because text-decoration
            // is a shorthand property, and it may contains other unrelated
            // longhand components, e.g. text-decoration-color, so we have to do
            // an extra check before setting |*aAll| to false.
            // e.g.
            //   firstValue: "underline rgb(0, 0, 0)"
            //   theValue: "underline rgb(0, 0, 238)" // <a> uses blue color
            // These two values should be the same if we are checking `<u>`.
            // That's why we need to check |*aFirst| and |isSet|.
            //
            // This is a work-around for text-decoration.
            // The spec issue: https://github.com/w3c/editing/issues/241.
            // Once this spec issue is resolved, we could drop this work-around
            // check.
            (!aStyle.IsStyleOfTextDecoration(
                 EditorInlineStyle::IgnoreSElement::Yes) ||
             *aFirst != isSet)) {
          *aAll = false;
        }
      }

      if (isSet) {
        *aAny = true;
      } else {
        *aAll = false;
      }
    }
  }
  if (!*aAny) {
    // make sure that if none of the selection is set, we don't report all is
    // set
    *aAll = false;
  }
  return NS_OK;
}

nsresult HTMLEditor::GetInlineProperty(nsStaticAtom& aHTMLProperty,
                                       nsAtom* aAttribute,
                                       const nsAString& aValue, bool* aFirst,
                                       bool* aAny, bool* aAll) const {
  if (NS_WARN_IF(!aFirst) || NS_WARN_IF(!aAny) || NS_WARN_IF(!aAll)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const nsAString* val = !aValue.IsEmpty() ? &aValue : nullptr;
  nsresult rv =
      GetInlinePropertyBase(EditorInlineStyle(aHTMLProperty, aAttribute), val,
                            aFirst, aAny, aAll, nullptr);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::GetInlinePropertyBase() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::GetInlinePropertyWithAttrValue(
    const nsAString& aHTMLProperty, const nsAString& aAttribute,
    const nsAString& aValue, bool* aFirst, bool* aAny, bool* aAll,
    nsAString& outValue) {
  nsStaticAtom* property = NS_GetStaticAtom(aHTMLProperty);
  if (NS_WARN_IF(!property)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsStaticAtom* attribute = EditorUtils::GetAttributeAtom(aAttribute);
  // MOZ_KnownLive because nsStaticAtom is available until shutting down.
  nsresult rv = GetInlinePropertyWithAttrValue(MOZ_KnownLive(*property),
                                               MOZ_KnownLive(attribute), aValue,
                                               aFirst, aAny, aAll, outValue);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::GetInlinePropertyWithAttrValue() failed");
  return rv;
}

nsresult HTMLEditor::GetInlinePropertyWithAttrValue(
    nsStaticAtom& aHTMLProperty, nsAtom* aAttribute, const nsAString& aValue,
    bool* aFirst, bool* aAny, bool* aAll, nsAString& outValue) {
  if (NS_WARN_IF(!aFirst) || NS_WARN_IF(!aAny) || NS_WARN_IF(!aAll)) {
    return NS_ERROR_INVALID_ARG;
  }

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const nsAString* val = !aValue.IsEmpty() ? &aValue : nullptr;
  nsresult rv =
      GetInlinePropertyBase(EditorInlineStyle(aHTMLProperty, aAttribute), val,
                            aFirst, aAny, aAll, &outValue);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::GetInlinePropertyBase() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::RemoveAllInlinePropertiesAsAction(
    nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(
      *this, EditAction::eRemoveAllInlineStyleProperties, aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || !IsStyleEditable(editingHost)) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eRemoveAllTextProperties, nsIEditor::eNext,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return EditorBase::ToGenericNSResult(ignoredError.StealNSResult());
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  AutoTArray<EditorInlineStyle, 1> removeAllInlineStyles;
  removeAllInlineStyles.AppendElement(EditorInlineStyle::RemoveAllStyles());
  rv = RemoveInlinePropertiesAsSubAction(removeAllInlineStyles, *editingHost);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::RemoveInlinePropertiesAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::RemoveInlinePropertyAsAction(nsStaticAtom& aHTMLProperty,
                                                  nsStaticAtom* aAttribute,
                                                  nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(
      *this,
      HTMLEditUtils::GetEditActionForFormatText(aHTMLProperty, aAttribute,
                                                false),
      aPrincipal);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || !IsStyleEditable(editingHost)) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  switch (editActionData.GetEditAction()) {
    case EditAction::eRemoveFontFamilyProperty:
      MOZ_ASSERT(!u""_ns.IsVoid());
      editActionData.SetData(u""_ns);
      break;
    case EditAction::eRemoveColorProperty:
    case EditAction::eRemoveBackgroundColorPropertyInline:
      editActionData.SetColorData(u""_ns);
      break;
    default:
      break;
  }
  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoTArray<EditorInlineStyle, 8> removeInlineStyleAndRelatedElements;
  AppendInlineStyleAndRelatedStyle(EditorInlineStyle(aHTMLProperty, aAttribute),
                                   removeInlineStyleAndRelatedElements);
  rv = RemoveInlinePropertiesAsSubAction(removeInlineStyleAndRelatedElements,
                                         *editingHost);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::RemoveInlinePropertiesAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

NS_IMETHODIMP HTMLEditor::RemoveInlineProperty(const nsAString& aProperty,
                                               const nsAString& aAttribute) {
  nsStaticAtom* property = NS_GetStaticAtom(aProperty);
  nsStaticAtom* attribute = EditorUtils::GetAttributeAtom(aAttribute);

  AutoEditActionDataSetter editActionData(
      *this,
      HTMLEditUtils::GetEditActionForFormatText(*property, attribute, false));
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  const RefPtr<Element> editingHost =
      ComputeEditingHost(LimitInBodyElement::No);
  if (!editingHost || !IsStyleEditable(editingHost)) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }

  switch (editActionData.GetEditAction()) {
    case EditAction::eRemoveFontFamilyProperty:
      MOZ_ASSERT(!EmptyString().IsVoid());
      editActionData.SetData(EmptyString());
      break;
    case EditAction::eRemoveColorProperty:
    case EditAction::eRemoveBackgroundColorPropertyInline:
      editActionData.SetColorData(EmptyString());
      break;
    default:
      break;
  }
  nsresult rv = editActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  AutoTArray<EditorInlineStyle, 1> removeOneInlineStyle;
  removeOneInlineStyle.AppendElement(EditorInlineStyle(*property, attribute));
  rv = RemoveInlinePropertiesAsSubAction(removeOneInlineStyle, *editingHost);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "HTMLEditor::RemoveInlinePropertiesAsSubAction() failed");
  return EditorBase::ToGenericNSResult(rv);
}

void HTMLEditor::AppendInlineStyleAndRelatedStyle(
    const EditorInlineStyle& aStyleToRemove,
    nsTArray<EditorInlineStyle>& aStylesToRemove) const {
  if (nsStaticAtom* similarElementName =
          aStyleToRemove.GetSimilarElementNameAtom()) {
    EditorInlineStyle anotherStyle(*similarElementName);
    if (!aStylesToRemove.Contains(anotherStyle)) {
      aStylesToRemove.AppendElement(std::move(anotherStyle));
    }
  } else if (aStyleToRemove.mHTMLProperty == nsGkAtoms::font) {
    if (aStyleToRemove.mAttribute == nsGkAtoms::size) {
      EditorInlineStyle big(*nsGkAtoms::big), small(*nsGkAtoms::small);
      if (!aStylesToRemove.Contains(big)) {
        aStylesToRemove.AppendElement(std::move(big));
      }
      if (!aStylesToRemove.Contains(small)) {
        aStylesToRemove.AppendElement(std::move(small));
      }
    }
    // Handling <tt> element code was implemented for composer (bug 115922).
    // This shouldn't work with Document.execCommand() for compatibility with
    // the other browsers.  Currently, edit action principal is set only when
    // the root caller is Document::ExecCommand() so that we should handle <tt>
    // element only when the principal is nullptr that must be only when XUL
    // command is executed on composer.
    else if (aStyleToRemove.mAttribute == nsGkAtoms::face &&
             !GetEditActionPrincipal()) {
      EditorInlineStyle tt(*nsGkAtoms::tt);
      if (!aStylesToRemove.Contains(tt)) {
        aStylesToRemove.AppendElement(std::move(tt));
      }
    }
  }
  if (!aStylesToRemove.Contains(aStyleToRemove)) {
    aStylesToRemove.AppendElement(aStyleToRemove);
  }
}

nsresult HTMLEditor::RemoveInlinePropertiesAsSubAction(
    const nsTArray<EditorInlineStyle>& aStylesToRemove,
    const Element& /*aEditingHost*/) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!aStylesToRemove.IsEmpty());

  DebugOnly<nsresult> rvIgnored = CommitComposition();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::CommitComposition() failed, but ignored");

  if (SelectionRef().IsCollapsed()) {
    // Manipulating text attributes on a collapsed selection only sets state
    // for the next text insertion
    mPendingStylesToApplyToNewContent->ClearStyles(aStylesToRemove);
    return NS_OK;
  }

  {
    Result<EditActionResult, nsresult> result = CanHandleHTMLEditSubAction();
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING("HTMLEditor::CanHandleHTMLEditSubAction() failed");
      return result.unwrapErr();
    }
    if (result.inspect().Canceled()) {
      return NS_OK;
    }
  }

  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eRemoveTextProperty, nsIEditor::eNext,
      ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // TODO: We don't need AutoTransactionsConserveSelection here in the normal
  //       cases, but removing this may cause the behavior with the legacy
  //       mutation event listeners.  We should try to delete this in a bug.
  AutoTransactionsConserveSelection dontChangeMySelection(*this);

  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());
  for (const EditorInlineStyle& styleToRemove : aStylesToRemove) {
    // The ranges may be updated by changing the DOM tree.  In strictly
    // speaking, we should save and restore the ranges at every range loop,
    // but we've never done so and it may be expensive if there are a lot of
    // ranges.  Therefore, we should do it for every style handling for now.
    // TODO: We should collect everything required for removing the style before
    //       touching the DOM tree.  Then, we need to save and restore the
    //       ranges only once.
    Maybe<AutoInlineStyleSetter> styleInverter;
    if (styleToRemove.IsInvertibleWithCSS()) {
      styleInverter.emplace(EditorInlineStyleAndValue::ToInvert(styleToRemove));
    }
    for (OwningNonNull<dom::Range>& selectionRange : selectionRanges.Ranges()) {
      AutoTrackDOMRange trackSelectionRange(RangeUpdaterRef(), &selectionRange);
      // If we're removing <a name>, we don't want to split ancestors because
      // the split fragment will keep working as named anchor.  Therefore, we
      // need to remove all <a name> elements which the selection range even
      // partially contains.
      const EditorDOMRange range(
          styleToRemove.mHTMLProperty == nsGkAtoms::name
              ? GetExtendedRangeWrappingNamedAnchor(
                    EditorRawDOMRange(selectionRange))
              : GetExtendedRangeWrappingEntirelySelectedElements(
                    EditorRawDOMRange(selectionRange)));
      if (NS_WARN_IF(!range.IsPositioned())) {
        continue;
      }

      // Remove this style from ancestors of our range endpoints, splitting
      // them as appropriate
      Result<SplitRangeOffResult, nsresult> splitRangeOffResult =
          SplitAncestorStyledInlineElementsAtRangeEdges(
              range, styleToRemove, SplitAtEdges::eAllowToCreateEmptyContainer);
      if (MOZ_UNLIKELY(splitRangeOffResult.isErr())) {
        NS_WARNING(
            "HTMLEditor::SplitAncestorStyledInlineElementsAtRangeEdges() "
            "failed");
        return splitRangeOffResult.unwrapErr();
      }
      // There is AutoTransactionsConserveSelection, so we don't need to
      // update selection here.
      splitRangeOffResult.inspect().IgnoreCaretPointSuggestion();

      // XXX Modifying `range` means that we may modify ranges in `Selection`.
      //     Is this intentional?  Note that the range may be not in
      //     `Selection` too.  It seems that at least one of them is not
      //     an unexpected case.
      const EditorDOMRange& splitRange =
          splitRangeOffResult.inspect().RangeRef();
      if (NS_WARN_IF(!splitRange.IsPositioned())) {
        continue;
      }

      AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContentsToInvertStyle;
      {
        // Collect top level children in the range first.
        // TODO: Perhaps, HTMLEditUtils::IsSplittableNode should be used here
        //       instead of EditorUtils::IsEditableContent.
        AutoTArray<OwningNonNull<nsIContent>, 64> arrayOfContentsAroundRange;
        if (splitRange.InSameContainer() &&
            splitRange.StartRef().IsInTextNode()) {
          if (!EditorUtils::IsEditableContent(
                  *splitRange.StartRef().ContainerAs<Text>(),
                  EditorType::HTML)) {
            continue;
          }
          arrayOfContentsAroundRange.AppendElement(
              *splitRange.StartRef().ContainerAs<Text>());
        } else if (splitRange.IsInTextNodes() &&
                   splitRange.InAdjacentSiblings()) {
          // Adjacent siblings are in a same element, so the editable state of
          // both text nodes are always same.
          if (!EditorUtils::IsEditableContent(
                  *splitRange.StartRef().ContainerAs<Text>(),
                  EditorType::HTML)) {
            continue;
          }
          arrayOfContentsAroundRange.AppendElement(
              *splitRange.StartRef().ContainerAs<Text>());
          arrayOfContentsAroundRange.AppendElement(
              *splitRange.EndRef().ContainerAs<Text>());
        } else {
          // Append first node if it's a text node but selected not entirely.
          if (splitRange.StartRef().IsInTextNode() &&
              !splitRange.StartRef().IsStartOfContainer() &&
              EditorUtils::IsEditableContent(
                  *splitRange.StartRef().ContainerAs<Text>(),
                  EditorType::HTML)) {
            arrayOfContentsAroundRange.AppendElement(
                *splitRange.StartRef().ContainerAs<Text>());
          }
          // Append all entirely selected nodes.
          ContentSubtreeIterator subtreeIter;
          if (NS_SUCCEEDED(
                  subtreeIter.Init(splitRange.StartRef().ToRawRangeBoundary(),
                                   splitRange.EndRef().ToRawRangeBoundary()))) {
            for (; !subtreeIter.IsDone(); subtreeIter.Next()) {
              nsCOMPtr<nsINode> node = subtreeIter.GetCurrentNode();
              if (NS_WARN_IF(!node)) {
                return NS_ERROR_FAILURE;
              }
              if (node->IsContent() &&
                  EditorUtils::IsEditableContent(*node->AsContent(),
                                                 EditorType::HTML)) {
                arrayOfContentsAroundRange.AppendElement(*node->AsContent());
              }
            }
          }
          // Append last node if it's a text node but selected not entirely.
          if (!splitRange.InSameContainer() &&
              splitRange.EndRef().IsInTextNode() &&
              !splitRange.EndRef().IsEndOfContainer() &&
              EditorUtils::IsEditableContent(
                  *splitRange.EndRef().ContainerAs<Text>(), EditorType::HTML)) {
            arrayOfContentsAroundRange.AppendElement(
                *splitRange.EndRef().ContainerAs<Text>());
          }
        }
        if (styleToRemove.IsInvertibleWithCSS()) {
          arrayOfContentsToInvertStyle.SetCapacity(
              arrayOfContentsAroundRange.Length());
        }

        for (OwningNonNull<nsIContent>& content : arrayOfContentsAroundRange) {
          // We should remove style from the element and its descendants.
          if (content->IsElement()) {
            Result<EditorDOMPoint, nsresult> removeStyleResult =
                RemoveStyleInside(MOZ_KnownLive(*content->AsElement()),
                                  styleToRemove, SpecifiedStyle::Preserve);
            if (MOZ_UNLIKELY(removeStyleResult.isErr())) {
              NS_WARNING("HTMLEditor::RemoveStyleInside() failed");
              return removeStyleResult.unwrapErr();
            }
            // There is AutoTransactionsConserveSelection, so we don't need to
            // update selection here.

            // If the element was removed from the DOM tree by
            // RemoveStyleInside, we need to do nothing for it anymore.
            if (!content->GetParentNode()) {
              continue;
            }
          }

          if (styleToRemove.IsInvertibleWithCSS()) {
            arrayOfContentsToInvertStyle.AppendElement(content);
          }
        }  // for-loop for arrayOfContentsAroundRange
      }

      auto FlushAndStopTrackingAndShrinkSelectionRange =
          [&]() MOZ_CAN_RUN_SCRIPT {
            trackSelectionRange.Flush(StopTracking::Yes);
            if (NS_WARN_IF(!selectionRange->IsPositioned())) {
              return;
            }
            EditorRawDOMRange range(selectionRange);
            nsINode* const commonAncestor =
                range.GetClosestCommonInclusiveAncestor();
            // Shrink range for compatibility between browsers.
            nsIContent* const maybeNextContent =
                range.StartRef().IsInContentNode() &&
                        range.StartRef().IsEndOfContainer()
                    ? AutoInlineStyleSetter::GetNextEditableInlineContent(
                          *range.StartRef().ContainerAs<nsIContent>(),
                          commonAncestor)
                    : nullptr;
            nsIContent* const maybePreviousContent =
                range.EndRef().IsInContentNode() &&
                        range.EndRef().IsStartOfContainer()
                    ? AutoInlineStyleSetter::GetPreviousEditableInlineContent(
                          *range.EndRef().ContainerAs<nsIContent>(),
                          commonAncestor)
                    : nullptr;
            if (!maybeNextContent && !maybePreviousContent) {
              return;
            }
            const auto startPoint =
                maybeNextContent &&
                        maybeNextContent != selectionRange->GetStartContainer()
                    ? HTMLEditUtils::GetDeepestEditableStartPointOf<
                          EditorRawDOMPoint>(
                          *maybeNextContent,
                          {EditablePointOption::RecognizeInvisibleWhiteSpaces,
                           EditablePointOption::StopAtComment})
                    : range.StartRef();
            const auto endPoint =
                maybePreviousContent && maybePreviousContent !=
                                            selectionRange->GetEndContainer()
                    ? HTMLEditUtils::GetDeepestEditableEndPointOf<
                          EditorRawDOMPoint>(
                          *maybePreviousContent,
                          {EditablePointOption::RecognizeInvisibleWhiteSpaces,
                           EditablePointOption::StopAtComment})
                    : range.EndRef();
            DebugOnly<nsresult> rvIgnored = selectionRange->SetStartAndEnd(
                startPoint.ToRawRangeBoundary(), endPoint.ToRawRangeBoundary());
            NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                                 "Range::SetStartAndEnd() failed, but ignored");
          };

      if (arrayOfContentsToInvertStyle.IsEmpty()) {
        FlushAndStopTrackingAndShrinkSelectionRange();
        continue;
      }
      MOZ_ASSERT(styleToRemove.IsInvertibleWithCSS());

      // If the style is specified in parent block and we can remove the
      // style with inserting new <span> element, we should do it.
      for (OwningNonNull<nsIContent>& content : arrayOfContentsToInvertStyle) {
        if (Element* element = Element::FromNode(content)) {
          // XXX Do we need to call this even when data node or something?  If
          //     so, for what?
          // MOZ_KnownLive because 'arrayOfContents' is guaranteed to
          // keep it alive.
          nsresult rv = styleInverter->InvertStyleIfApplied(
              *this, MOZ_KnownLive(*element));
          if (NS_FAILED(rv)) {
            if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
              NS_WARNING(
                  "AutoInlineStyleSetter::InvertStyleIfApplied() failed");
              return NS_ERROR_EDITOR_DESTROYED;
            }
            NS_WARNING(
                "AutoInlineStyleSetter::InvertStyleIfApplied() failed, but "
                "ignored");
          }
          continue;
        }

        // Unfortunately, all browsers don't join text nodes when removing a
        // style.  Therefore, there may be multiple text nodes as adjacent
        // siblings.  That's the reason why we need to handle text nodes in this
        // loop.
        if (Text* textNode = Text::FromNode(content)) {
          const uint32_t startOffset =
              content == splitRange.StartRef().GetContainer()
                  ? splitRange.StartRef().Offset()
                  : 0u;
          const uint32_t endOffset =
              content == splitRange.EndRef().GetContainer()
                  ? splitRange.EndRef().Offset()
                  : textNode->TextDataLength();
          Result<SplitRangeOffFromNodeResult, nsresult>
              wrapTextInStyledElementResult =
                  styleInverter->InvertStyleIfApplied(
                      *this, MOZ_KnownLive(*textNode), startOffset, endOffset);
          if (MOZ_UNLIKELY(wrapTextInStyledElementResult.isErr())) {
            NS_WARNING("AutoInlineStyleSetter::InvertStyleIfApplied() failed");
            return wrapTextInStyledElementResult.unwrapErr();
          }
          SplitRangeOffFromNodeResult unwrappedWrapTextInStyledElementResult =
              wrapTextInStyledElementResult.unwrap();
          // There is AutoTransactionsConserveSelection, so we don't need to
          // update selection here.
          unwrappedWrapTextInStyledElementResult.IgnoreCaretPointSuggestion();
          // If we've split the content, let's swap content in
          // arrayOfContentsToInvertStyle with the text node which is applied
          // the style.
          if (unwrappedWrapTextInStyledElementResult.DidSplit() &&
              styleToRemove.IsInvertibleWithCSS()) {
            MOZ_ASSERT(unwrappedWrapTextInStyledElementResult
                           .GetMiddleContentAs<Text>());
            if (Text* styledTextNode = unwrappedWrapTextInStyledElementResult
                                           .GetMiddleContentAs<Text>()) {
              if (styledTextNode != content) {
                arrayOfContentsToInvertStyle.ReplaceElementAt(
                    arrayOfContentsToInvertStyle.Length() - 1,
                    OwningNonNull<nsIContent>(*styledTextNode));
              }
            }
          }
          continue;
        }

        // If the node is not an element nor a text node, it's invisible.
        // In this case, we don't need to make it wrapped in new element.
      }

      // Finally, we should remove the style from all leaf text nodes if
      // they still have the style.
      AutoTArray<OwningNonNull<Text>, 32> leafTextNodes;
      for (const OwningNonNull<nsIContent>& content :
           arrayOfContentsToInvertStyle) {
        // XXX Should we ignore content which has already removed from the
        //     DOM tree by the previous for-loop?
        if (content->IsElement()) {
          CollectEditableLeafTextNodes(*content->AsElement(), leafTextNodes);
        }
      }
      for (const OwningNonNull<Text>& textNode : leafTextNodes) {
        Result<SplitRangeOffFromNodeResult, nsresult>
            wrapTextInStyledElementResult = styleInverter->InvertStyleIfApplied(
                *this, MOZ_KnownLive(*textNode), 0, textNode->TextLength());
        if (MOZ_UNLIKELY(wrapTextInStyledElementResult.isErr())) {
          NS_WARNING(
              "AutoInlineStyleSetter::SplitTextNodeAndApplyStyleToMiddleNode() "
              "failed");
          return wrapTextInStyledElementResult.unwrapErr();
        }
        // There is AutoTransactionsConserveSelection, so we don't need to
        // update selection here.
        wrapTextInStyledElementResult.inspect().IgnoreCaretPointSuggestion();
      }  // for-loop of leafTextNodes

      // styleInverter may have touched a part of the range.  Therefore, we
      // cannot adjust the range without comparing DOM node position and
      // first/last touched positions, but it may be too expensive.  I think
      // that shrinking only the tracked range boundaries must be enough in most
      // cases.
      FlushAndStopTrackingAndShrinkSelectionRange();
    }  // for-loop of selectionRanges
  }  // for-loop of styles

  MOZ_ASSERT(!selectionRanges.HasSavedRanges());
  nsresult rv = selectionRanges.ApplyTo(SelectionRef());
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoClonedSelectionRangeArray::ApplyTo() failed");
  return rv;
}

nsresult HTMLEditor::AutoInlineStyleSetter::InvertStyleIfApplied(
    HTMLEditor& aHTMLEditor, Element& aElement) {
  MOZ_ASSERT(IsStyleToInvert());

  Result<bool, nsresult> isRemovableParentStyleOrError =
      aHTMLEditor.IsRemovableParentStyleWithNewSpanElement(aElement, *this);
  if (MOZ_UNLIKELY(isRemovableParentStyleOrError.isErr())) {
    NS_WARNING("HTMLEditor::IsRemovableParentStyleWithNewSpanElement() failed");
    return isRemovableParentStyleOrError.unwrapErr();
  }
  if (!isRemovableParentStyleOrError.unwrap()) {
    // E.g., text-decoration cannot be override visually in children.
    // In such cases, we can do nothing.
    return NS_OK;
  }

  // Wrap it into a new element, move it into direct child which has same style,
  // or specify the style to its parent.
  Result<CaretPoint, nsresult> pointToPutCaretOrError =
      ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle(aHTMLEditor, aElement);
  if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
    NS_WARNING(
        "AutoInlineStyleSetter::"
        "ApplyStyleToNodeOrChildrenAndRemoveNestedSameStyle() failed");
    return pointToPutCaretOrError.unwrapErr();
  }
  // The caller must update `Selection` later so that we don't need this.
  pointToPutCaretOrError.unwrap().IgnoreCaretPointSuggestion();
  return NS_OK;
}

Result<SplitRangeOffFromNodeResult, nsresult>
HTMLEditor::AutoInlineStyleSetter::InvertStyleIfApplied(HTMLEditor& aHTMLEditor,
                                                        Text& aTextNode,
                                                        uint32_t aStartOffset,
                                                        uint32_t aEndOffset) {
  MOZ_ASSERT(IsStyleToInvert());

  Result<bool, nsresult> isRemovableParentStyleOrError =
      aHTMLEditor.IsRemovableParentStyleWithNewSpanElement(aTextNode, *this);
  if (MOZ_UNLIKELY(isRemovableParentStyleOrError.isErr())) {
    NS_WARNING("HTMLEditor::IsRemovableParentStyleWithNewSpanElement() failed");
    return isRemovableParentStyleOrError.propagateErr();
  }
  if (!isRemovableParentStyleOrError.unwrap()) {
    // E.g., text-decoration cannot be override visually in children.
    // In such cases, we can do nothing.
    return SplitRangeOffFromNodeResult(nullptr, &aTextNode, nullptr);
  }

  // We need to use new `<span>` element or existing element if it's available
  // to overwrite parent style.
  Result<SplitRangeOffFromNodeResult, nsresult> wrapTextInStyledElementResult =
      SplitTextNodeAndApplyStyleToMiddleNode(aHTMLEditor, aTextNode,
                                             aStartOffset, aEndOffset);
  NS_WARNING_ASSERTION(
      wrapTextInStyledElementResult.isOk(),
      "AutoInlineStyleSetter::SplitTextNodeAndApplyStyleToMiddleNode() failed");
  return wrapTextInStyledElementResult;
}

Result<bool, nsresult> HTMLEditor::IsRemovableParentStyleWithNewSpanElement(
    nsIContent& aContent, const EditorInlineStyle& aStyle) const {
  // We don't support to remove all inline styles with this path.
  if (aStyle.IsStyleToClearAllInlineStyles()) {
    return false;
  }

  // First check whether the style is invertible since this is the fastest
  // check.
  if (!aStyle.IsInvertibleWithCSS()) {
    return false;
  }

  // If aContent is not an element and it's not in an element, it means that
  // aContent is disconnected non-element node.  In this case, it's never
  // applied any styles which are invertible.
  const RefPtr<Element> element = aContent.GetAsElementOrParentElement();
  if (MOZ_UNLIKELY(!element)) {
    return false;
  }

  // If parent block has invertible style, we should remove the style with
  // creating new `<span>` element even in HTML mode because Chrome does it.
  if (!aStyle.IsCSSSettable(*element)) {
    return false;
  }
  nsAutoString emptyString;
  Result<bool, nsresult> isComputedCSSEquivalentToStyleOrError =
      CSSEditUtils::IsComputedCSSEquivalentTo(*this, *element, aStyle,
                                              emptyString);
  NS_WARNING_ASSERTION(isComputedCSSEquivalentToStyleOrError.isOk(),
                       "CSSEditUtils::IsComputedCSSEquivalentTo() failed");
  return isComputedCSSEquivalentToStyleOrError;
}

void HTMLEditor::CollectEditableLeafTextNodes(
    Element& aElement, nsTArray<OwningNonNull<Text>>& aLeafTextNodes) const {
  for (nsIContent* child = aElement.GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsElement()) {
      CollectEditableLeafTextNodes(*child->AsElement(), aLeafTextNodes);
      continue;
    }
    if (child->IsText()) {
      aLeafTextNodes.AppendElement(*child->AsText());
    }
  }
}

nsresult HTMLEditor::IncreaseFontSizeAsAction(nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eIncrementFontSize,
                                          aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = IncrementOrDecrementFontSizeAsSubAction(FontSize::incr);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::IncrementOrDecrementFontSizeAsSubAction("
                       "FontSize::incr) failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::DecreaseFontSizeAsAction(nsIPrincipal* aPrincipal) {
  AutoEditActionDataSetter editActionData(*this, EditAction::eDecrementFontSize,
                                          aPrincipal);
  nsresult rv = editActionData.CanHandleAndMaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "CanHandleAndMaybeDispatchBeforeInputEvent(), failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  rv = IncrementOrDecrementFontSizeAsSubAction(FontSize::decr);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "HTMLEditor::IncrementOrDecrementFontSizeAsSubAction("
                       "FontSize::decr) failed");
  return EditorBase::ToGenericNSResult(rv);
}

nsresult HTMLEditor::IncrementOrDecrementFontSizeAsSubAction(
    FontSize aIncrementOrDecrement) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // Committing composition and changing font size should be undone together.
  AutoPlaceholderBatch treatAsOneTransaction(
      *this, ScrollSelectionIntoView::Yes, __FUNCTION__);

  DebugOnly<nsresult> rvIgnored = CommitComposition();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::CommitComposition() failed, but ignored");

  // If selection is collapsed, set typing state
  if (SelectionRef().IsCollapsed()) {
    nsStaticAtom& bigOrSmallTagName = aIncrementOrDecrement == FontSize::incr
                                          ? *nsGkAtoms::big
                                          : *nsGkAtoms::small;

    // Let's see in what kind of element the selection is
    if (!SelectionRef().RangeCount()) {
      return NS_OK;
    }
    const auto firstRangeStartPoint =
        EditorBase::GetFirstSelectionStartPoint<EditorRawDOMPoint>();
    if (NS_WARN_IF(!firstRangeStartPoint.IsSet())) {
      return NS_OK;
    }
    Element* element =
        firstRangeStartPoint.GetContainerOrContainerParentElement();
    if (NS_WARN_IF(!element)) {
      return NS_OK;
    }
    if (!HTMLEditUtils::CanNodeContain(*element, bigOrSmallTagName)) {
      return NS_OK;
    }

    // Manipulating text attributes on a collapsed selection only sets state
    // for the next text insertion
    mPendingStylesToApplyToNewContent->PreserveStyle(bigOrSmallTagName, nullptr,
                                                     u""_ns);
    return NS_OK;
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eSetTextProperty, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "HTMLEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  // TODO: We don't need AutoTransactionsConserveSelection here in the normal
  //       cases, but removing this may cause the behavior with the legacy
  //       mutation event listeners.  We should try to delete this in a bug.
  AutoTransactionsConserveSelection dontChangeMySelection(*this);

  AutoClonedSelectionRangeArray selectionRanges(SelectionRef());
  MOZ_ALWAYS_TRUE(selectionRanges.SaveAndTrackRanges(*this));
  for (const OwningNonNull<dom::Range>& domRange : selectionRanges.Ranges()) {
    // TODO: We should stop extending the range outside ancestor blocks because
    //       we don't need to do it for setting inline styles.  However, here is
    //       chrome only handling path.  Therefore, we don't need to fix here
    //       soon.
    const EditorDOMRange range(GetExtendedRangeWrappingEntirelySelectedElements(
        EditorRawDOMRange(domRange)));
    if (NS_WARN_IF(!range.IsPositioned())) {
      continue;
    }

    if (range.InSameContainer() && range.StartRef().IsInTextNode()) {
      Result<CreateElementResult, nsresult> wrapInBigOrSmallElementResult =
          SetFontSizeOnTextNode(
              MOZ_KnownLive(*range.StartRef().ContainerAs<Text>()),
              range.StartRef().Offset(), range.EndRef().Offset(),
              aIncrementOrDecrement);
      if (MOZ_UNLIKELY(wrapInBigOrSmallElementResult.isErr())) {
        NS_WARNING("HTMLEditor::SetFontSizeOnTextNode() failed");
        return wrapInBigOrSmallElementResult.unwrapErr();
      }
      // There is an AutoTransactionsConserveSelection instance so that we don't
      // need to update selection for this change.
      wrapInBigOrSmallElementResult.inspect().IgnoreCaretPointSuggestion();
      continue;
    }

    // Not the easy case.  Range not contained in single text node.  There
    // are up to three phases here.  There are all the nodes reported by the
    // subtree iterator to be processed.  And there are potentially a
    // starting textnode and an ending textnode which are only partially
    // contained by the range.

    // Let's handle the nodes reported by the iterator.  These nodes are
    // entirely contained in the selection range.  We build up a list of them
    // (since doing operations on the document during iteration would perturb
    // the iterator).

    // Iterate range and build up array
    ContentSubtreeIterator subtreeIter;
    if (NS_SUCCEEDED(subtreeIter.Init(range.StartRef().ToRawRangeBoundary(),
                                      range.EndRef().ToRawRangeBoundary()))) {
      nsTArray<OwningNonNull<nsIContent>> arrayOfContents;
      for (; !subtreeIter.IsDone(); subtreeIter.Next()) {
        if (NS_WARN_IF(!subtreeIter.GetCurrentNode()->IsContent())) {
          return NS_ERROR_FAILURE;
        }
        OwningNonNull<nsIContent> content =
            *subtreeIter.GetCurrentNode()->AsContent();

        if (EditorUtils::IsEditableContent(content, EditorType::HTML)) {
          arrayOfContents.AppendElement(content);
        }
      }

      // Now that we have the list, do the font size change on each node
      for (OwningNonNull<nsIContent>& content : arrayOfContents) {
        // MOZ_KnownLive because of bug 1622253
        Result<EditorDOMPoint, nsresult> fontChangeOnNodeResult =
            SetFontSizeWithBigOrSmallElement(MOZ_KnownLive(content),
                                             aIncrementOrDecrement);
        if (MOZ_UNLIKELY(fontChangeOnNodeResult.isErr())) {
          NS_WARNING("HTMLEditor::SetFontSizeWithBigOrSmallElement() failed");
          return fontChangeOnNodeResult.unwrapErr();
        }
        // There is an AutoTransactionsConserveSelection, so we don't need to
        // update selection here.
      }
    }
    // Now check the start and end parents of the range to see if they need
    // to be separately handled (they do if they are text nodes, due to how
    // the subtree iterator works - it will not have reported them).
    if (range.StartRef().IsInTextNode() &&
        !range.StartRef().IsEndOfContainer() &&
        EditorUtils::IsEditableContent(*range.StartRef().ContainerAs<Text>(),
                                       EditorType::HTML)) {
      Result<CreateElementResult, nsresult> wrapInBigOrSmallElementResult =
          SetFontSizeOnTextNode(
              MOZ_KnownLive(*range.StartRef().ContainerAs<Text>()),
              range.StartRef().Offset(),
              range.StartRef().ContainerAs<Text>()->TextDataLength(),
              aIncrementOrDecrement);
      if (MOZ_UNLIKELY(wrapInBigOrSmallElementResult.isErr())) {
        NS_WARNING("HTMLEditor::SetFontSizeOnTextNode() failed");
        return wrapInBigOrSmallElementResult.unwrapErr();
      }
      // There is an AutoTransactionsConserveSelection instance so that we
      // don't need to update selection for this change.
      wrapInBigOrSmallElementResult.inspect().IgnoreCaretPointSuggestion();
    }
    if (range.EndRef().IsInTextNode() && !range.EndRef().IsStartOfContainer() &&
        EditorUtils::IsEditableContent(*range.EndRef().ContainerAs<Text>(),
                                       EditorType::HTML)) {
      Result<CreateElementResult, nsresult> wrapInBigOrSmallElementResult =
          SetFontSizeOnTextNode(
              MOZ_KnownLive(*range.EndRef().ContainerAs<Text>()), 0u,
              range.EndRef().Offset(), aIncrementOrDecrement);
      if (MOZ_UNLIKELY(wrapInBigOrSmallElementResult.isErr())) {
        NS_WARNING("HTMLEditor::SetFontSizeOnTextNode() failed");
        return wrapInBigOrSmallElementResult.unwrapErr();
      }
      // There is an AutoTransactionsConserveSelection instance so that we
      // don't need to update selection for this change.
      wrapInBigOrSmallElementResult.inspect().IgnoreCaretPointSuggestion();
    }
  }

  MOZ_ASSERT(selectionRanges.HasSavedRanges());
  selectionRanges.RestoreFromSavedRanges();
  nsresult rv = selectionRanges.ApplyTo(SelectionRef());
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "AutoClonedSelectionRangeArray::ApplyTo() failed");
  return rv;
}

Result<CreateElementResult, nsresult> HTMLEditor::SetFontSizeOnTextNode(
    Text& aTextNode, uint32_t aStartOffset, uint32_t aEndOffset,
    FontSize aIncrementOrDecrement) {
  // Don't need to do anything if no characters actually selected
  if (aStartOffset == aEndOffset) {
    return CreateElementResult::NotHandled();
  }

  if (!aTextNode.GetParentNode() ||
      !HTMLEditUtils::CanNodeContain(*aTextNode.GetParentNode(),
                                     *nsGkAtoms::big)) {
    return CreateElementResult::NotHandled();
  }

  aEndOffset = std::min(aTextNode.Length(), aEndOffset);

  // Make the range an independent node.
  RefPtr<Text> textNodeForTheRange = &aTextNode;

  EditorDOMPoint pointToPutCaret;
  {
    auto pointToPutCaretOrError =
        [&]() MOZ_CAN_RUN_SCRIPT -> Result<EditorDOMPoint, nsresult> {
      EditorDOMPoint pointToPutCaret;
      // Split at the end of the range.
      EditorDOMPoint atEnd(textNodeForTheRange, aEndOffset);
      if (!atEnd.IsEndOfContainer()) {
        // We need to split off back of text node
        Result<SplitNodeResult, nsresult> splitAtEndResult =
            SplitNodeWithTransaction(atEnd);
        if (MOZ_UNLIKELY(splitAtEndResult.isErr())) {
          NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
          return splitAtEndResult.propagateErr();
        }
        SplitNodeResult unwrappedSplitAtEndResult = splitAtEndResult.unwrap();
        if (MOZ_UNLIKELY(
                !unwrappedSplitAtEndResult.HasCaretPointSuggestion())) {
          NS_WARNING(
              "HTMLEditor::SplitNodeWithTransaction() didn't suggest caret "
              "point");
          return Err(NS_ERROR_FAILURE);
        }
        unwrappedSplitAtEndResult.MoveCaretPointTo(pointToPutCaret, *this, {});
        MOZ_ASSERT_IF(AllowsTransactionsToChangeSelection(),
                      pointToPutCaret.IsSet());
        textNodeForTheRange =
            unwrappedSplitAtEndResult.GetPreviousContentAs<Text>();
        MOZ_DIAGNOSTIC_ASSERT(textNodeForTheRange);
      }

      // Split at the start of the range.
      EditorDOMPoint atStart(textNodeForTheRange, aStartOffset);
      if (!atStart.IsStartOfContainer()) {
        // We need to split off front of text node
        Result<SplitNodeResult, nsresult> splitAtStartResult =
            SplitNodeWithTransaction(atStart);
        if (MOZ_UNLIKELY(splitAtStartResult.isErr())) {
          NS_WARNING("HTMLEditor::SplitNodeWithTransaction() failed");
          return splitAtStartResult.propagateErr();
        }
        SplitNodeResult unwrappedSplitAtStartResult =
            splitAtStartResult.unwrap();
        if (MOZ_UNLIKELY(
                !unwrappedSplitAtStartResult.HasCaretPointSuggestion())) {
          NS_WARNING(
              "HTMLEditor::SplitNodeWithTransaction() didn't suggest caret "
              "point");
          return Err(NS_ERROR_FAILURE);
        }
        unwrappedSplitAtStartResult.MoveCaretPointTo(pointToPutCaret, *this,
                                                     {});
        MOZ_ASSERT_IF(AllowsTransactionsToChangeSelection(),
                      pointToPutCaret.IsSet());
        textNodeForTheRange =
            unwrappedSplitAtStartResult.GetNextContentAs<Text>();
        MOZ_DIAGNOSTIC_ASSERT(textNodeForTheRange);
      }

      return pointToPutCaret;
    }();
    if (MOZ_UNLIKELY(pointToPutCaretOrError.isErr())) {
      // Don't warn here since it should be done in the lambda.
      return pointToPutCaretOrError.propagateErr();
    }
    pointToPutCaret = pointToPutCaretOrError.unwrap();
  }

  // Look for siblings that are correct type of node
  nsStaticAtom* const bigOrSmallTagName =
      aIncrementOrDecrement == FontSize::incr ? nsGkAtoms::big
                                              : nsGkAtoms::small;
  nsCOMPtr<nsIContent> sibling = HTMLEditUtils::GetPreviousSibling(
      *textNodeForTheRange, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (sibling && sibling->IsHTMLElement(bigOrSmallTagName)) {
    // Previous sib is already right kind of inline node; slide this over
    Result<MoveNodeResult, nsresult> moveTextNodeResult =
        MoveNodeToEndWithTransaction(*textNodeForTheRange, *sibling);
    if (MOZ_UNLIKELY(moveTextNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
      return moveTextNodeResult.propagateErr();
    }
    MoveNodeResult unwrappedMoveTextNodeResult = moveTextNodeResult.unwrap();
    unwrappedMoveTextNodeResult.MoveCaretPointTo(
        pointToPutCaret, *this, {SuggestCaret::OnlyIfHasSuggestion});
    // XXX Should we return the new container?
    return CreateElementResult::NotHandled(std::move(pointToPutCaret));
  }
  sibling = HTMLEditUtils::GetNextSibling(
      *textNodeForTheRange, {LeafNodeOption::IgnoreNonEditableNode},
      BlockInlineCheck::UseComputedDisplayOutsideStyle);
  if (sibling && sibling->IsHTMLElement(bigOrSmallTagName)) {
    // Following sib is already right kind of inline node; slide this over
    Result<MoveNodeResult, nsresult> moveTextNodeResult =
        MoveNodeWithTransaction(*textNodeForTheRange,
                                EditorDOMPoint(sibling, 0u));
    if (MOZ_UNLIKELY(moveTextNodeResult.isErr())) {
      NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
      return moveTextNodeResult.propagateErr();
    }
    MoveNodeResult unwrappedMoveTextNodeResult = moveTextNodeResult.unwrap();
    unwrappedMoveTextNodeResult.MoveCaretPointTo(
        pointToPutCaret, *this, {SuggestCaret::OnlyIfHasSuggestion});
    // XXX Should we return the new container?
    return CreateElementResult::NotHandled(std::move(pointToPutCaret));
  }

  // Else wrap the node inside font node with appropriate relative size
  Result<CreateElementResult, nsresult> wrapTextInBigOrSmallElementResult =
      InsertContainerWithTransaction(*textNodeForTheRange,
                                     MOZ_KnownLive(*bigOrSmallTagName));
  if (wrapTextInBigOrSmallElementResult.isErr()) {
    NS_WARNING("HTMLEditor::InsertContainerWithTransaction() failed");
    return wrapTextInBigOrSmallElementResult;
  }
  CreateElementResult unwrappedWrapTextInBigOrSmallElementResult =
      wrapTextInBigOrSmallElementResult.unwrap();
  unwrappedWrapTextInBigOrSmallElementResult.MoveCaretPointTo(
      pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
  return CreateElementResult(
      unwrappedWrapTextInBigOrSmallElementResult.UnwrapNewNode(),
      std::move(pointToPutCaret));
}

Result<EditorDOMPoint, nsresult> HTMLEditor::SetFontSizeOfFontElementChildren(
    nsIContent& aContent, FontSize aIncrementOrDecrement) {
  // This routine looks for all the font nodes in the tree rooted by aNode,
  // including aNode itself, looking for font nodes that have the size attr
  // set.  Any such nodes need to have big or small put inside them, since
  // they override any big/small that are above them.

  // If this is a font node with size, put big/small inside it.
  if (aContent.IsHTMLElement(nsGkAtoms::font) &&
      aContent.AsElement()->HasAttr(nsGkAtoms::size)) {
    EditorDOMPoint pointToPutCaret;

    // Cycle through children and adjust relative font size.
    AutoTArray<OwningNonNull<nsIContent>, 32> arrayOfContents;
    HTMLEditUtils::CollectAllChildren(aContent, arrayOfContents);
    for (const auto& child : arrayOfContents) {
      // MOZ_KnownLive because of bug 1622253
      Result<EditorDOMPoint, nsresult> setFontSizeOfChildResult =
          SetFontSizeWithBigOrSmallElement(MOZ_KnownLive(child),
                                           aIncrementOrDecrement);
      if (MOZ_UNLIKELY(setFontSizeOfChildResult.isErr())) {
        NS_WARNING("HTMLEditor::WrapContentInBigOrSmallElement() failed");
        return setFontSizeOfChildResult;
      }
      if (setFontSizeOfChildResult.inspect().IsSet()) {
        pointToPutCaret = setFontSizeOfChildResult.unwrap();
      }
    }

    // WrapContentInBigOrSmallElement already calls us recursively,
    // so we don't need to check our children again.
    return pointToPutCaret;
  }

  // Otherwise cycle through the children.
  EditorDOMPoint pointToPutCaret;
  AutoTArray<OwningNonNull<nsIContent>, 32> arrayOfContents;
  HTMLEditUtils::CollectAllChildren(aContent, arrayOfContents);
  for (const auto& child : arrayOfContents) {
    // MOZ_KnownLive because of bug 1622253
    Result<EditorDOMPoint, nsresult> fontSizeChangeResult =
        SetFontSizeOfFontElementChildren(MOZ_KnownLive(child),
                                         aIncrementOrDecrement);
    if (MOZ_UNLIKELY(fontSizeChangeResult.isErr())) {
      NS_WARNING("HTMLEditor::SetFontSizeOfFontElementChildren() failed");
      return fontSizeChangeResult;
    }
    if (fontSizeChangeResult.inspect().IsSet()) {
      pointToPutCaret = fontSizeChangeResult.unwrap();
    }
  }

  return pointToPutCaret;
}

Result<EditorDOMPoint, nsresult> HTMLEditor::SetFontSizeWithBigOrSmallElement(
    nsIContent& aContent, FontSize aIncrementOrDecrement) {
  nsStaticAtom* const bigOrSmallTagName =
      aIncrementOrDecrement == FontSize::incr ? nsGkAtoms::big
                                              : nsGkAtoms::small;

  // Is aContent the opposite of what we want?
  if ((aIncrementOrDecrement == FontSize::incr &&
       aContent.IsHTMLElement(nsGkAtoms::small)) ||
      (aIncrementOrDecrement == FontSize::decr &&
       aContent.IsHTMLElement(nsGkAtoms::big))) {
    // First, populate any nested font elements that have the size attr set
    Result<EditorDOMPoint, nsresult> fontSizeChangeOfDescendantsResult =
        SetFontSizeOfFontElementChildren(aContent, aIncrementOrDecrement);
    if (MOZ_UNLIKELY(fontSizeChangeOfDescendantsResult.isErr())) {
      NS_WARNING("HTMLEditor::SetFontSizeOfFontElementChildren() failed");
      return fontSizeChangeOfDescendantsResult;
    }
    EditorDOMPoint pointToPutCaret = fontSizeChangeOfDescendantsResult.unwrap();
    // In that case, just unwrap the <big> or <small> element.
    Result<EditorDOMPoint, nsresult> unwrapBigOrSmallElementResult =
        RemoveContainerWithTransaction(MOZ_KnownLive(*aContent.AsElement()));
    if (MOZ_UNLIKELY(unwrapBigOrSmallElementResult.isErr())) {
      NS_WARNING("HTMLEditor::RemoveContainerWithTransaction() failed");
      return unwrapBigOrSmallElementResult;
    }
    if (unwrapBigOrSmallElementResult.inspect().IsSet()) {
      pointToPutCaret = unwrapBigOrSmallElementResult.unwrap();
    }
    return pointToPutCaret;
  }

  if (HTMLEditUtils::CanNodeContain(*bigOrSmallTagName, aContent)) {
    // First, populate any nested font tags that have the size attr set
    Result<EditorDOMPoint, nsresult> fontSizeChangeOfDescendantsResult =
        SetFontSizeOfFontElementChildren(aContent, aIncrementOrDecrement);
    if (MOZ_UNLIKELY(fontSizeChangeOfDescendantsResult.isErr())) {
      NS_WARNING("HTMLEditor::SetFontSizeOfFontElementChildren() failed");
      return fontSizeChangeOfDescendantsResult;
    }

    EditorDOMPoint pointToPutCaret = fontSizeChangeOfDescendantsResult.unwrap();

    // Next, if next or previous is <big> or <small>, move aContent into it.
    nsCOMPtr<nsIContent> sibling = HTMLEditUtils::GetPreviousSibling(
        aContent, {LeafNodeOption::IgnoreNonEditableNode},
        BlockInlineCheck::UseComputedDisplayOutsideStyle);
    if (sibling && sibling->IsHTMLElement(bigOrSmallTagName)) {
      Result<MoveNodeResult, nsresult> moveNodeResult =
          MoveNodeToEndWithTransaction(aContent, *sibling);
      if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeToEndWithTransaction() failed");
        return moveNodeResult.propagateErr();
      }
      MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
      unwrappedMoveNodeResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      return pointToPutCaret;
    }

    sibling = HTMLEditUtils::GetNextSibling(
        aContent, {LeafNodeOption::IgnoreNonEditableNode},
        BlockInlineCheck::UseComputedDisplayOutsideStyle);
    if (sibling && sibling->IsHTMLElement(bigOrSmallTagName)) {
      Result<MoveNodeResult, nsresult> moveNodeResult =
          MoveNodeWithTransaction(aContent, EditorDOMPoint(sibling, 0u));
      if (MOZ_UNLIKELY(moveNodeResult.isErr())) {
        NS_WARNING("HTMLEditor::MoveNodeWithTransaction() failed");
        return moveNodeResult.propagateErr();
      }
      MoveNodeResult unwrappedMoveNodeResult = moveNodeResult.unwrap();
      unwrappedMoveNodeResult.MoveCaretPointTo(
          pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
      return pointToPutCaret;
    }

    // Otherwise, wrap aContent in new <big> or <small>
    Result<CreateElementResult, nsresult> wrapInBigOrSmallElementResult =
        InsertContainerWithTransaction(aContent,
                                       MOZ_KnownLive(*bigOrSmallTagName));
    if (MOZ_UNLIKELY(wrapInBigOrSmallElementResult.isErr())) {
      NS_WARNING("HTMLEditor::InsertContainerWithTransaction() failed");
      return Err(wrapInBigOrSmallElementResult.unwrapErr());
    }
    CreateElementResult unwrappedWrapInBigOrSmallElementResult =
        wrapInBigOrSmallElementResult.unwrap();
    MOZ_ASSERT(unwrappedWrapInBigOrSmallElementResult.GetNewNode());
    unwrappedWrapInBigOrSmallElementResult.MoveCaretPointTo(
        pointToPutCaret, {SuggestCaret::OnlyIfHasSuggestion});
    return pointToPutCaret;
  }

  // none of the above?  then cycle through the children.
  // MOOSE: we should group the children together if possible
  // into a single "big" or "small".  For the moment they are
  // each getting their own.
  EditorDOMPoint pointToPutCaret;
  AutoTArray<OwningNonNull<nsIContent>, 32> arrayOfContents;
  HTMLEditUtils::CollectAllChildren(aContent, arrayOfContents);
  for (const auto& child : arrayOfContents) {
    // MOZ_KnownLive because of bug 1622253
    Result<EditorDOMPoint, nsresult> setFontSizeOfChildResult =
        SetFontSizeWithBigOrSmallElement(MOZ_KnownLive(child),
                                         aIncrementOrDecrement);
    if (MOZ_UNLIKELY(setFontSizeOfChildResult.isErr())) {
      NS_WARNING("HTMLEditor::SetFontSizeWithBigOrSmallElement() failed");
      return setFontSizeOfChildResult;
    }
    if (setFontSizeOfChildResult.inspect().IsSet()) {
      pointToPutCaret = setFontSizeOfChildResult.unwrap();
    }
  }

  return pointToPutCaret;
}

NS_IMETHODIMP HTMLEditor::GetFontFaceState(bool* aMixed, nsAString& outFace) {
  if (NS_WARN_IF(!aMixed)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aMixed = true;
  outFace.Truncate();

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  bool first, any, all;

  nsresult rv = GetInlinePropertyBase(
      EditorInlineStyle(*nsGkAtoms::font, nsGkAtoms::face), nullptr, &first,
      &any, &all, &outFace);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::GetInlinePropertyBase(nsGkAtoms::font, nsGkAtoms::face) "
        "failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (any && !all) {
    return NS_OK;  // mixed
  }
  if (all) {
    *aMixed = false;
    return NS_OK;
  }

  // if there is no font face, check for tt
  rv = GetInlinePropertyBase(EditorInlineStyle(*nsGkAtoms::tt), nullptr, &first,
                             &any, &all, nullptr);
  if (NS_FAILED(rv)) {
    NS_WARNING("HTMLEditor::GetInlinePropertyBase(nsGkAtoms::tt) failed");
    return EditorBase::ToGenericNSResult(rv);
  }
  if (any && !all) {
    return NS_OK;  // mixed
  }
  if (all) {
    *aMixed = false;
    outFace.AssignLiteral("tt");
  }

  if (!any) {
    // there was no font face attrs of any kind.  We are in normal font.
    outFace.Truncate();
    *aMixed = false;
  }
  return NS_OK;
}

nsresult HTMLEditor::GetFontColorState(bool* aMixed, nsAString& aOutColor) {
  if (NS_WARN_IF(!aMixed)) {
    return NS_ERROR_INVALID_ARG;
  }

  *aMixed = true;
  aOutColor.Truncate();

  AutoEditActionDataSetter editActionData(*this, EditAction::eNotEditing);
  if (NS_WARN_IF(!editActionData.CanHandle())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  bool first, any, all;
  nsresult rv = GetInlinePropertyBase(
      EditorInlineStyle(*nsGkAtoms::font, nsGkAtoms::color), nullptr, &first,
      &any, &all, &aOutColor);
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "HTMLEditor::GetInlinePropertyBase(nsGkAtoms::font, nsGkAtoms::color) "
        "failed");
    return EditorBase::ToGenericNSResult(rv);
  }

  if (any && !all) {
    return NS_OK;  // mixed
  }
  if (all) {
    *aMixed = false;
    return NS_OK;
  }

  if (!any) {
    // there was no font color attrs of any kind..
    aOutColor.Truncate();
    *aMixed = false;
  }
  return NS_OK;
}

// The return value is true only if the instance of the HTML editor we created
// can handle CSS styles and if the CSS preference is checked.
NS_IMETHODIMP HTMLEditor::GetIsCSSEnabled(bool* aIsCSSEnabled) {
  *aIsCSSEnabled = IsCSSEnabled();
  return NS_OK;
}

bool HTMLEditor::HasStyleOrIdOrClassAttribute(Element& aElement) {
  return aElement.HasNonEmptyAttr(nsGkAtoms::style) ||
         aElement.HasNonEmptyAttr(nsGkAtoms::_class) ||
         aElement.HasNonEmptyAttr(nsGkAtoms::id);
}

}  // namespace mozilla
