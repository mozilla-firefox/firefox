/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ErrorList.h"
#include "TextEditor.h"

#include "AutoClonedRangeArray.h"
#include "EditAction.h"
#include "EditorDOMPoint.h"
#include "EditorUtils.h"
#include "HTMLEditor.h"

#include "mozilla/Assertions.h"
#include "mozilla/Logging.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_editor.h"
#include "mozilla/TextComposition.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/HTMLBRElement.h"
#include "mozilla/dom/NodeFilterBinding.h"
#include "mozilla/dom/NodeIterator.h"
#include "mozilla/dom/Selection.h"

#include "nsAString.h"
#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsCRTGlue.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsGkAtoms.h"
#include "mozilla/dom/ContentList.h"
#include "mozilla/Utf16.h"
#include "nsIContent.h"
#include "nsINode.h"
#include "nsISupports.h"
#include "nsLiteralString.h"
#include "nsNameSpaceManager.h"
#include "nsPrintfCString.h"
#include "nsTextNode.h"
#include "nsUnicharUtils.h"

namespace mozilla {

extern LazyLogModule gTextInputLog;  // Defined in EditorBase.cpp

using namespace dom;

#define CANCEL_OPERATION_AND_RETURN_EDIT_ACTION_RESULT_IF_READONLY \
  if (IsReadonly()) {                                              \
    return EditActionResult::CanceledResult();                     \
  }

void TextEditor::OnStartToHandleTopLevelEditSubAction(
    EditSubAction aTopLevelEditSubAction,
    nsIEditor::EDirection aDirectionOfTopLevelEditSubAction, ErrorResult& aRv) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!aRv.Failed());

  EditorBase::OnStartToHandleTopLevelEditSubAction(
      aTopLevelEditSubAction, aDirectionOfTopLevelEditSubAction, aRv);

  MOZ_ASSERT(GetTopLevelEditSubAction() == aTopLevelEditSubAction);
  MOZ_ASSERT(GetDirectionOfTopLevelEditSubAction() ==
             aDirectionOfTopLevelEditSubAction);

  if (NS_WARN_IF(Destroyed())) {
    aRv.Throw(NS_ERROR_EDITOR_DESTROYED);
    return;
  }

  if (NS_WARN_IF(!mInitSucceeded)) {
    return;
  }

  if (aTopLevelEditSubAction == EditSubAction::eSetText) {
    // SetText replaces all text, so spell checker handles starting from the
    // start of new value.
    SetSpellCheckRestartPoint(EditorDOMPoint(mRootElement, 0));
    return;
  }

  if (aTopLevelEditSubAction == EditSubAction::eInsertText ||
      aTopLevelEditSubAction == EditSubAction::eInsertTextComingFromIME) {
    // For spell checker, previous selected node should be text node if
    // possible. If anchor is root of editor, it may become invalid offset
    // after inserting text.
    const EditorRawDOMPoint point =
        FindBetterInsertionPoint(EditorRawDOMPoint(SelectionRef().AnchorRef()));
    if (point.IsSet()) {
      SetSpellCheckRestartPoint(point);
      return;
    }
    NS_WARNING("TextEditor::FindBetterInsertionPoint() failed, but ignored");
  }
  if (SelectionRef().AnchorRef().IsSet()) {
    SetSpellCheckRestartPoint(EditorRawDOMPoint(SelectionRef().AnchorRef()));
  }
}

nsresult TextEditor::OnEndHandlingTopLevelEditSubAction() {
  MOZ_ASSERT(IsTopLevelEditSubActionDataAvailable());

  nsresult rv;
  while (true) {
    if (NS_WARN_IF(Destroyed())) {
      rv = NS_ERROR_EDITOR_DESTROYED;
      break;
    }

    // XXX Probably, we should spellcheck again after edit action (not top-level
    //     sub-action) is handled because the ranges can be referred only by
    //     users.
    if (NS_FAILED(rv = HandleInlineSpellCheckAfterEdit())) {
      NS_WARNING("TextEditor::HandleInlineSpellCheckAfterEdit() failed");
      break;
    }

    if (!IsSingleLineEditor() &&
        NS_FAILED(rv = EnsurePaddingBRElementInMultilineEditor())) {
      NS_WARNING(
          "EditorBase::EnsurePaddingBRElementInMultilineEditor() failed");
      break;
    }

    rv = EnsureCaretNotAtEndOfTextNode();
    if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
      break;
    }
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "TextEditor::EnsureCaretNotAtEndOfTextNode() failed, but ignored");
    rv = NS_OK;
    break;
  }
  DebugOnly<nsresult> rvIgnored =
      EditorBase::OnEndHandlingTopLevelEditSubAction();
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rvIgnored),
      "EditorBase::OnEndHandlingTopLevelEditSubAction() failed, but ignored");
  MOZ_ASSERT(!GetTopLevelEditSubAction());
  MOZ_ASSERT(GetDirectionOfTopLevelEditSubAction() == eNone);
  return rv;
}

nsresult TextEditor::InsertLineBreakAsSubAction() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  if (NS_WARN_IF(!mInitSucceeded)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  IgnoredErrorResult ignoredError;
  AutoEditSubActionNotifier startToHandleEditSubAction(
      *this, EditSubAction::eInsertLineBreak, nsIEditor::eNext, ignoredError);
  if (NS_WARN_IF(ignoredError.ErrorCodeIs(NS_ERROR_EDITOR_DESTROYED))) {
    return ignoredError.StealNSResult();
  }
  NS_WARNING_ASSERTION(
      !ignoredError.Failed(),
      "TextEditor::OnStartToHandleTopLevelEditSubAction() failed, but ignored");

  Result<EditActionResult, nsresult> result =
      InsertLineFeedCharacterAtSelection();
  if (MOZ_UNLIKELY(result.isErr())) {
    NS_WARNING(
        "TextEditor::InsertLineFeedCharacterAtSelection() failed, but ignored");
    return result.unwrapErr();
  }
  return NS_OK;
}

Result<EditActionResult, nsresult>
TextEditor::InsertLineFeedCharacterAtSelection() {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!IsSingleLineEditor());

  UndefineCaretBidiLevel();

  CANCEL_OPERATION_AND_RETURN_EDIT_ACTION_RESULT_IF_READONLY

  if (mMaxTextLength >= 0) {
    nsAutoString insertionString(u"\n"_ns);
    Result<EditActionResult, nsresult> result =
        MaybeTruncateInsertionStringForMaxLength(insertionString);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING(
          "TextEditor::MaybeTruncateInsertionStringForMaxLength() failed");
      return result;
    }
    if (result.inspect().Handled()) {
      // Don't return as handled since we stopped inserting the line break.
      return EditActionResult::CanceledResult();
    }
  }

  // if the selection isn't collapsed, delete it.
  if (!SelectionRef().IsCollapsed()) {
    nsresult rv =
        DeleteSelectionAsSubAction(nsIEditor::eNone, nsIEditor::eNoStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::DeleteSelectionAsSubAction(eNone, eNoStrip) failed");
      return Err(rv);
    }
  }

  const auto pointToInsert = GetFirstSelectionStartPoint<EditorDOMPoint>();
  if (NS_WARN_IF(!pointToInsert.IsSet())) {
    return Err(NS_ERROR_FAILURE);
  }
  MOZ_ASSERT(pointToInsert.IsSetAndValid());
  MOZ_ASSERT(!pointToInsert.IsContainerHTMLElement(nsGkAtoms::br));

  // Insert a linefeed character.
  Result<InsertTextResult, nsresult> insertTextResult =
      InsertTextWithTransaction(u"\n"_ns, pointToInsert,
                                InsertTextTo::ExistingTextNodeIfAvailable,
                                InsertTextFor::NormalText);
  if (MOZ_UNLIKELY(insertTextResult.isErr())) {
    NS_WARNING("TextEditor::InsertTextWithTransaction(\"\\n\") failed");
    return insertTextResult.propagateErr();
  }
  insertTextResult.inspect().IgnoreCaretPointSuggestion();
  EditorDOMPoint pointToPutCaret =
      insertTextResult.inspect().Handled()
          ? insertTextResult.inspect().EndOfInsertedTextRef()
          : pointToInsert;
  if (NS_WARN_IF(!pointToPutCaret.IsSetAndValid())) {
    return Err(NS_ERROR_FAILURE);
  }
  // XXX I don't think we still need this.  This must have been required when
  //     `<textarea>` was implemented with text nodes and `<br>` elements.
  // We want the caret to stick to the content on the "right".  We want the
  // caret to stick to whatever is past the break.  This is because the break is
  // on the same line we were on, but the next content will be on the following
  // line.
  pointToPutCaret.SetInterlinePosition(InterlinePosition::StartOfNextLine);
  nsresult rv = CollapseSelectionTo(pointToPutCaret);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::CollapseSelectionTo() failed");
    return Err(rv);
  }
  return EditActionResult::HandledResult();
}

nsresult TextEditor::EnsureCaretNotAtEndOfTextNode() {
  MOZ_ASSERT(IsEditActionDataAvailable());

  // If there is no selection ranges, we should set to the end of the editor.
  // This is usually performed in InitEditorContentAndSelection(), however,
  // if the editor is reframed, this may be called by
  // OnEndHandlingTopLevelEditSubAction().
  if (SelectionRef().RangeCount()) {
    return NS_OK;
  }

  nsresult rv = CollapseSelectionToEndOfTextNode();
  if (MOZ_UNLIKELY(rv == NS_ERROR_EDITOR_DESTROYED)) {
    NS_WARNING(
        "TextEditor::CollapseSelectionToEndOfTextNode() caused destroying the "
        "editor");
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "TextEditor::CollapseSelectionToEndOfTextNode() failed, but ignored");

  return NS_OK;
}

void TextEditor::HandleNewLinesInStringForSingleLineEditor(
    nsString& aString) const {
  static const char16_t kLF = static_cast<char16_t>('\n');
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aString.FindChar(static_cast<uint16_t>('\r')) == kNotFound);

  // First of all, check if aString contains '\n' since if the string
  // does not include it, we don't need to do nothing here.
  int32_t firstLF = aString.FindChar(kLF, 0);
  if (firstLF == kNotFound) {
    return;
  }

  switch (mNewlineHandling) {
    case nsIEditor::eNewlinesReplaceWithSpaces:
      // Default of Firefox:
      // Strip trailing newlines first so we don't wind up with trailing spaces
      aString.Trim(LFSTR, false, true);
      aString.ReplaceChar(kLF, ' ');
      break;
    case nsIEditor::eNewlinesStrip:
      aString.StripChar(kLF);
      break;
    case nsIEditor::eNewlinesPasteToFirst:
    default: {
      // we get first *non-empty* line.
      int32_t offset = 0;
      while (firstLF == offset) {
        offset++;
        firstLF = aString.FindChar(kLF, offset);
      }
      if (firstLF > 0) {
        aString.Truncate(firstLF);
      }
      if (offset > 0) {
        aString.Cut(0, offset);
      }
      break;
    }
    case nsIEditor::eNewlinesReplaceWithCommas:
      // Default of Thunderbird:
      aString.Trim(LFSTR, true, true);
      aString.ReplaceChar(kLF, ',');
      break;
    case nsIEditor::eNewlinesStripSurroundingWhitespace: {
      nsAutoString result;
      uint32_t offset = 0;
      while (offset < aString.Length()) {
        int32_t nextLF = !offset ? firstLF : aString.FindChar(kLF, offset);
        if (nextLF < 0) {
          result.Append(nsDependentSubstring(aString, offset));
          break;
        }
        uint32_t wsBegin = nextLF;
        // look backwards for the first non-white-space char
        while (wsBegin > offset && NS_IS_SPACE(aString[wsBegin - 1])) {
          --wsBegin;
        }
        result.Append(nsDependentSubstring(aString, offset, wsBegin - offset));
        offset = nextLF + 1;
        while (offset < aString.Length() && NS_IS_SPACE(aString[offset])) {
          ++offset;
        }
      }
      aString = std::move(result);
      break;
    }
    case nsIEditor::eNewlinesPasteIntact:
      // even if we're pasting newlines, don't paste leading/trailing ones
      aString.Trim(LFSTR, true, true);
      break;
  }
}

Result<EditActionResult, nsresult> TextEditor::HandleInsertText(
    const nsAString& aInsertionString, InsertTextFor aPurpose) {
  MOZ_ASSERT(IsEditActionDataAvailable());

  MOZ_LOG(
      gTextInputLog, LogLevel::Info,
      ("%p TextEditor::HandleInsertText(aInsertionString=\"%s\", aPurpose=%s)",
       this, NS_ConvertUTF16toUTF8(aInsertionString).get(),
       ToString(aPurpose).c_str()));

  UndefineCaretBidiLevel();

  nsAutoString insertionString(aInsertionString);
  if (!aInsertionString.IsEmpty() && mMaxTextLength >= 0) {
    Result<EditActionResult, nsresult> result =
        MaybeTruncateInsertionStringForMaxLength(insertionString);
    if (MOZ_UNLIKELY(result.isErr())) {
      NS_WARNING(
          "TextEditor::MaybeTruncateInsertionStringForMaxLength() failed");
      EditActionResult unwrappedResult = result.unwrap();
      unwrappedResult.MarkAsHandled();
      return unwrappedResult;
    }
    // If we're exceeding the maxlength when composing IME, we need to clean up
    // the composing text, so we shouldn't return early.
    if (result.inspect().Handled() && insertionString.IsEmpty() &&
        NothingToDoIfInsertingEmptyText(aPurpose)) {
      return EditActionResult::CanceledResult();
    }
  }

  uint32_t start = 0;
  if (IsPasswordEditor()) {
    if (GetComposition() && !GetComposition()->String().IsEmpty()) {
      start = GetComposition()->ClampedStartOffsetInTextNode();
    } else {
      uint32_t end = 0;
      nsContentUtils::GetSelectionInTextControl(&SelectionRef(), GetRoot(),
                                                start, end);
    }
  }

  // if the selection isn't collapsed, delete it.
  if (!SelectionRef().IsCollapsed() &&
      !InsertingTextForExtantComposition(aPurpose)) {
    nsresult rv =
        DeleteSelectionAsSubAction(nsIEditor::eNone, nsIEditor::eNoStrip);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "EditorBase::DeleteSelectionAsSubAction(eNone, eNoStrip) failed");
      return Err(rv);
    }
  }

  if (aInsertionString.IsEmpty() && NothingToDoIfInsertingEmptyText(aPurpose)) {
    // HACK: this is a fix for bug 19395
    // I can't outlaw all empty insertions
    // because IME transaction depend on them
    // There is more work to do to make the
    // world safe for IME.
    return EditActionResult::CanceledResult();
  }

  // XXX Why don't we cancel here?  Shouldn't we do this first?
  CANCEL_OPERATION_AND_RETURN_EDIT_ACTION_RESULT_IF_READONLY

  MaybeDoAutoPasswordMasking();

  // People have lots of different ideas about what text fields
  // should do with multiline pastes.  See bugs 21032, 23485, 23485, 50935.
  // The six possible options are:
  // 0. paste newlines intact
  // 1. paste up to the first newline (default)
  // 2. replace newlines with spaces
  // 3. strip newlines
  // 4. replace with commas
  // 5. strip newlines and surrounding white-space
  // So find out what we're expected to do:
  if (IsSingleLineEditor()) {
    // XXX Some callers of TextEditor::InsertTextAsAction()  already make the
    //     string use only \n as a linebreaker.  However, they are not hot
    //     path and nsContentUtils::PlatformToDOMLineBreaks() does nothing
    //     if the string doesn't include \r.  So, let's convert linebreakers
    //     here.  Note that there are too many callers of
    //     TextEditor::InsertTextAsAction().  So, it's difficult to keep
    //     maintaining all of them won't reach here without \r nor \r\n.
    // XXX Should we handle do this before truncating the string for
    //     `maxlength`?
    nsContentUtils::PlatformToDOMLineBreaks(insertionString);
    HandleNewLinesInStringForSingleLineEditor(insertionString);
  }

  const auto atStartOfSelection = GetFirstSelectionStartPoint<EditorDOMPoint>();
  if (NS_WARN_IF(!atStartOfSelection.IsSetAndValid())) {
    return Err(NS_ERROR_FAILURE);
  }
  MOZ_ASSERT(!atStartOfSelection.IsContainerHTMLElement(nsGkAtoms::br));

  if (InsertingTextForComposition(aPurpose)) {
    EditorDOMPoint compositionStartPoint =
        GetFirstIMESelectionStartPoint<EditorDOMPoint>();
    if (!compositionStartPoint.IsSet()) {
      compositionStartPoint = FindBetterInsertionPoint(atStartOfSelection);
      NS_WARNING_ASSERTION(
          compositionStartPoint.IsSet(),
          "TextEditor::FindBetterInsertionPoint() failed, but ignored");
    }
    Result<InsertTextResult, nsresult> insertTextResult =
        InsertTextWithTransaction(insertionString, compositionStartPoint,
                                  InsertTextTo::ExistingTextNodeIfAvailable,
                                  aPurpose);
    if (MOZ_UNLIKELY(insertTextResult.isErr())) {
      NS_WARNING("EditorBase::InsertTextWithTransaction() failed");
      return insertTextResult.propagateErr();
    }
    nsresult rv = insertTextResult.unwrap().SuggestCaretPointTo(
        *this, {SuggestCaret::OnlyIfHasSuggestion,
                SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                SuggestCaret::AndIgnoreTrivialError});
    if (NS_FAILED(rv)) {
      NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
      return Err(rv);
    }
    NS_WARNING_ASSERTION(
        rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
        "CaretPoint::SuggestCaretPointTo() failed, but ignored");
  } else {
    MOZ_ASSERT(!InsertingTextForComposition(aPurpose));

    Result<InsertTextResult, nsresult> insertTextResult =
        InsertTextWithTransaction(insertionString, atStartOfSelection,
                                  InsertTextTo::ExistingTextNodeIfAvailable,
                                  aPurpose);
    if (MOZ_UNLIKELY(insertTextResult.isErr())) {
      NS_WARNING("EditorBase::InsertTextWithTransaction() failed");
      return insertTextResult.propagateErr();
    }
    // Ignore caret suggestion because there was
    // AutoTransactionsConserveSelection.
    insertTextResult.inspect().IgnoreCaretPointSuggestion();
    if (insertTextResult.inspect().Handled()) {
      // Make the caret attach to the inserted text, unless this text ends with
      // a LF, in which case make the caret attach to the next line.
      const bool endsWithLF =
          !insertionString.IsEmpty() && insertionString.Last() == nsCRT::LF;
      EditorDOMPoint pointToPutCaret =
          insertTextResult.inspect().EndOfInsertedTextRef();
      pointToPutCaret.SetInterlinePosition(
          endsWithLF ? InterlinePosition::StartOfNextLine
                     : InterlinePosition::EndOfLine);
      MOZ_ASSERT(pointToPutCaret.IsInTextNode(),
                 "After inserting text into a text node, insertTextResult "
                 "should return a point in a text node");
      nsresult rv = CollapseSelectionTo(pointToPutCaret);
      if (NS_WARN_IF(rv == NS_ERROR_EDITOR_DESTROYED)) {
        return Err(NS_ERROR_EDITOR_DESTROYED);
      }
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "EditorBase::CollapseSelectionTo() failed, but ignored");
    }
  }

  // Unmask inputted character(s) if necessary.
  if (IsPasswordEditor() && IsMaskingPassword() && CanEchoPasswordNow()) {
    nsresult rv = SetUnmaskRangeAndNotify(start, insertionString.Length(),
                                          LookAndFeel::GetPasswordMaskDelay());
    if (NS_FAILED(rv)) {
      NS_WARNING("TextEditor::SetUnmaskRangeAndNotify() failed");
      return Err(rv);
    }
    return EditActionResult::HandledResult();
  }

  return EditActionResult::HandledResult();
}

Result<EditActionResult, nsresult> TextEditor::SetTextWithoutTransaction(
    const nsAString& aValue) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!IsIMEComposing());
  MOZ_ASSERT(!IsUndoRedoEnabled());
  MOZ_ASSERT(GetEditAction() != EditAction::eReplaceText);
  MOZ_ASSERT(mMaxTextLength < 0);
  MOZ_ASSERT(aValue.FindChar(static_cast<char16_t>('\r')) == kNotFound);

  UndefineCaretBidiLevel();

  // XXX If we're setting value, shouldn't we keep setting the new value here?
  CANCEL_OPERATION_AND_RETURN_EDIT_ACTION_RESULT_IF_READONLY

  MaybeDoAutoPasswordMasking();

  const RefPtr<Text> textNode = GetTextNode();
  MOZ_ASSERT(textNode);

  // We can use this fast path only when:
  //  - we need to insert a text node.
  //  - we need to replace content of existing text node.
  // Additionally, for avoiding odd result, we should check whether we're in
  // usual condition.
  if (!IsSingleLineEditor()) {
    // If we're a multiline text editor, i.e., <textarea>, there is a padding
    // <br> element for empty last line followed by scrollbar/resizer elements.
    // Otherwise, a text node is followed by them.
    if (!textNode->GetNextSibling() ||
        !EditorUtils::IsPaddingBRElementForEmptyLastLine(
            *textNode->GetNextSibling())) {
      return EditActionResult::IgnoredResult();
    }
  }

  // XXX Password fields accept line breaks as normal characters with this code.
  //     Is this intentional?
  nsAutoString sanitizedValue(aValue);
  if (IsSingleLineEditor() && !IsPasswordEditor()) {
    HandleNewLinesInStringForSingleLineEditor(sanitizedValue);
  }

  nsresult rv = SetTextNodeWithoutTransaction(sanitizedValue, *textNode);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::SetTextNodeWithoutTransaction() failed");
    return Err(rv);
  }

  return EditActionResult::HandledResult();
}

Result<EditActionResult, nsresult> TextEditor::HandleDeleteSelection(
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aStripWrappers == nsIEditor::eNoStrip);

  UndefineCaretBidiLevel();

  CANCEL_OPERATION_AND_RETURN_EDIT_ACTION_RESULT_IF_READONLY

  if (IsEmpty()) {
    return EditActionResult::CanceledResult();
  }
  Result<EditActionResult, nsresult> result =
      HandleDeleteSelectionInternal(aDirectionAndAmount, nsIEditor::eNoStrip);
  // HandleDeleteSelectionInternal() creates SelectionBatcher.  Therefore,
  // quitting from it might cause having destroyed the editor.
  if (NS_WARN_IF(Destroyed())) {
    return Err(NS_ERROR_EDITOR_DESTROYED);
  }
  NS_WARNING_ASSERTION(
      result.isOk(),
      "TextEditor::HandleDeleteSelectionInternal(eNoStrip) failed");
  return result;
}

Result<EditActionResult, nsresult> TextEditor::HandleDeleteSelectionInternal(
    nsIEditor::EDirection aDirectionAndAmount,
    nsIEditor::EStripWrappers aStripWrappers) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(aStripWrappers == nsIEditor::eNoStrip);

  // If the current selection is empty (e.g the user presses backspace with
  // a collapsed selection), then we want to avoid sending the selectstart
  // event to the user, so we hide selection changes. However, we still
  // want to send a single selectionchange event to the document, so we
  // batch the selectionchange events, such that a single event fires after
  // the AutoHideSelectionChanges destructor has been run.
  SelectionBatcher selectionBatcher(SelectionRef(), __FUNCTION__);
  AutoHideSelectionChanges hideSelection(SelectionRef());
  nsAutoScriptBlocker scriptBlocker;

  if (IsPasswordEditor() && IsMaskingPassword()) {
    MaskAllCharacters();
  } else {
    const auto selectionStartPoint =
        GetFirstSelectionStartPoint<EditorRawDOMPoint>();
    if (NS_WARN_IF(!selectionStartPoint.IsSet())) {
      return Err(NS_ERROR_FAILURE);
    }

    if (!SelectionRef().IsCollapsed()) {
      AutoClonedSelectionRangeArray rangesToDelete(SelectionRef());
      if (NS_WARN_IF(rangesToDelete.Ranges().IsEmpty())) {
        NS_ASSERTION(false,
                     "For avoiding to throw incompatible exception for "
                     "`execCommand`, fix the caller");
        return Err(NS_ERROR_FAILURE);
      }

      if (const Text* const textNode = GetTextNode()) {
        rangesToDelete.EnsureRangesInTextNode(*textNode);
      }
      rangesToDelete.MaybeExtendAnchorFocusRangeToDeleteAdjacentWhiteSpace(
          *this);

      Result<CaretPoint, nsresult> caretPointOrError =
          DeleteRangesWithTransaction(aDirectionAndAmount, aStripWrappers,
                                      rangesToDelete);
      if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
        NS_WARNING("EditorBase::DeleteRangesWithTransaction() failed");
        return caretPointOrError.propagateErr();
      }
      nsresult rv = caretPointOrError.inspect().SuggestCaretPointTo(
          *this, {SuggestCaret::OnlyIfHasSuggestion,
                  SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
                  SuggestCaret::AndIgnoreTrivialError});
      if (NS_FAILED(rv)) {
        NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
      }
      NS_WARNING_ASSERTION(
          rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
          "CaretPoint::SuggestCaretPointTo() failed, but ignored");
      return EditActionResult::HandledResult();
    }

    // Test for distance between caret and text that will be deleted
    AutoCaretBidiLevelManager bidiLevelManager(*this, aDirectionAndAmount,
                                               selectionStartPoint);
    if (MOZ_UNLIKELY(bidiLevelManager.Failed())) {
      NS_WARNING("EditorBase::AutoCaretBidiLevelManager() failed");
      return Err(NS_ERROR_FAILURE);
    }
    bidiLevelManager.MaybeUpdateCaretBidiLevel(*this);
    if (bidiLevelManager.Canceled()) {
      return EditActionResult::CanceledResult();
    }
  }

  AutoClonedSelectionRangeArray rangesToDelete(SelectionRef());
  Result<nsIEditor::EDirection, nsresult> result =
      rangesToDelete.ExtendAnchorFocusRangeFor(*this, aDirectionAndAmount);
  if (result.isErr()) [[unlikely]] {
    // Couldn't extend the range to outside the limiter. Don't warn this nor
    // return error since the user may press Backspace or Delete at start or
    // end of the anonymous <div>.
    return EditActionResult::HandledResult();
  }
  if (const Text* theTextNode = GetTextNode()) {
    rangesToDelete.EnsureRangesInTextNode(*theTextNode);
  }

  Result<CaretPoint, nsresult> caretPointOrError = DeleteRangesWithTransaction(
      result.unwrap(), nsIEditor::eNoStrip, rangesToDelete);
  if (MOZ_UNLIKELY(caretPointOrError.isErr())) {
    NS_WARNING("EditorBase::DeleteRangesWithTransaction(eNoStrip) failed");
    return caretPointOrError.propagateErr();
  }

  nsresult rv = caretPointOrError.inspect().SuggestCaretPointTo(
      *this, {SuggestCaret::OnlyIfHasSuggestion,
              SuggestCaret::OnlyIfTransactionsAllowedToDoIt,
              SuggestCaret::AndIgnoreTrivialError});
  if (NS_FAILED(rv)) {
    NS_WARNING("CaretPoint::SuggestCaretPointTo() failed");
    return Err(rv);
  }
  NS_WARNING_ASSERTION(rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
                       "CaretPoint::SuggestCaretPointTo() failed, but ignored");

  return EditActionResult::HandledResult();
}

Result<EditActionResult, nsresult>
TextEditor::ComputeValueFromTextNodeAndBRElement(nsAString& aValue) const {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(!IsHTMLEditor());

  Element* anonymousDivElement = GetRoot();
  if (MOZ_UNLIKELY(!anonymousDivElement)) {
    // Don't warn this case, this is possible, e.g., 997805.html
    aValue.Truncate();
    return EditActionResult::HandledResult();
  }

  const Text* const textNode = GetTextNode();
  MOZ_ASSERT(textNode);

  if (!textNode->Length()) {
    aValue.Truncate();
    return EditActionResult::HandledResult();
  }

  nsIContent* firstChildExceptText = textNode->GetNextSibling();
  // If the DOM tree is unexpected, fall back to the expensive path.
  bool isInput = IsSingleLineEditor();
  bool isTextarea = !isInput;
  if (NS_WARN_IF(isInput && firstChildExceptText) ||
      NS_WARN_IF(isTextarea && !firstChildExceptText) ||
      NS_WARN_IF(isTextarea &&
                 !EditorUtils::IsPaddingBRElementForEmptyLastLine(
                     *firstChildExceptText) &&
                 !firstChildExceptText->IsXULElement(nsGkAtoms::scrollbar))) {
    return EditActionResult::IgnoredResult();
  }

  // Otherwise, the text data is the value.
  textNode->GetData(aValue);
  return EditActionResult::HandledResult();
}

Result<EditActionResult, nsresult>
TextEditor::MaybeTruncateInsertionStringForMaxLength(
    nsAString& aInsertionString) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(mMaxTextLength >= 0);

  if (IsIMEComposing()) {
    return EditActionResult::IgnoredResult();
  }

  // Ignore user pastes
  switch (GetEditAction()) {
    case EditAction::ePaste:
    case EditAction::ePasteAsQuotation:
    case EditAction::eDrop:
    case EditAction::eReplaceText:
      // EditActionPrinciple() is non-null iff the edit was requested by
      // javascript.
      if (!GetEditActionPrincipal()) {
        // By now we are certain that this is a user paste, before we ignore it,
        // lets check if the user explictly enabled truncating user pastes.
        if (!StaticPrefs::editor_truncate_user_pastes()) {
          return EditActionResult::IgnoredResult();
        }
      }
      [[fallthrough]];
    default:
      break;
  }

  uint32_t currentLength = UINT32_MAX;
  nsresult rv = GetTextLength(&currentLength);
  if (NS_FAILED(rv)) {
    NS_WARNING("TextEditor::GetTextLength() failed");
    return Err(rv);
  }

  uint32_t selectionStart, selectionEnd;
  nsContentUtils::GetSelectionInTextControl(&SelectionRef(), GetRoot(),
                                            selectionStart, selectionEnd);

  TextComposition* composition = GetComposition();
  const uint32_t kOldCompositionStringLength =
      composition ? composition->String().Length() : 0;

  const uint32_t kSelectionLength = selectionEnd - selectionStart;
  // XXX This computation must be wrong.  If we'll support non-collapsed
  //     selection even during composition for Korean IME, kSelectionLength
  //     is part of kOldCompositionStringLength.
  const uint32_t kNewLength =
      currentLength - kSelectionLength - kOldCompositionStringLength;
  if (kNewLength >= AssertedCast<uint32_t>(mMaxTextLength)) {
    aInsertionString.Truncate();  // Too long, we cannot accept new character.
    return EditActionResult::HandledResult();
  }

  if (aInsertionString.Length() + kNewLength <=
      AssertedCast<uint32_t>(mMaxTextLength)) {
    return EditActionResult::IgnoredResult();  // Enough short string.
  }

  int32_t newInsertionStringLength = mMaxTextLength - kNewLength;
  MOZ_ASSERT(newInsertionStringLength > 0);
  char16_t maybeHighSurrogate =
      aInsertionString.CharAt(newInsertionStringLength - 1);
  char16_t maybeLowSurrogate =
      aInsertionString.CharAt(newInsertionStringLength);
  // Don't split the surrogate pair.
  if (mozilla::IsSurrogatePair(maybeHighSurrogate, maybeLowSurrogate)) {
    newInsertionStringLength--;
  }
  // XXX What should we do if we're removing IVS but its preceding
  //     character won't be removed?
  aInsertionString.Truncate(newInsertionStringLength);
  return EditActionResult::HandledResult();
}

bool TextEditor::CanEchoPasswordNow() const {
  if (!LookAndFeel::GetEchoPassword() || EchoingPasswordPrevented()) {
    return false;
  }

  return GetEditAction() != EditAction::eDrop &&
         GetEditAction() != EditAction::ePaste;
}

}  // namespace mozilla
