/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TextEditor.h"

#include "EditorUtils.h"
#include "HTMLEditor.h"
#include "SelectionState.h"

#include "mozilla/MouseEvents.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Range.h"
#include "mozilla/dom/Selection.h"

#include "nsAString.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsIClipboard.h"
#include "nsIContent.h"
#include "nsIDragService.h"
#include "nsIDragSession.h"
#include "nsIPrincipal.h"
#include "nsIFormControl.h"
#include "nsISupportsPrimitives.h"
#include "nsITransferable.h"
#include "nsIVariant.h"
#include "nsLiteralString.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsXPCOM.h"
#include "nscore.h"

namespace mozilla {

using namespace dom;

nsresult TextEditor::InsertTextFromTransferable(
    nsITransferable* aTransferable) {
  MOZ_ASSERT(IsEditActionDataAvailable());
  MOZ_ASSERT(IsTextEditor());

  nsAutoCString bestFlavor;
  nsCOMPtr<nsISupports> genericDataObj;
  nsresult rv = aTransferable->GetAnyTransferData(
      bestFlavor, getter_AddRefs(genericDataObj));
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "nsITransferable::GetAnyDataTransferData() failed, but ignored");
  if (NS_SUCCEEDED(rv) && (bestFlavor.EqualsLiteral(kTextMime) ||
                           bestFlavor.EqualsLiteral(kMozTextInternal) ||
                           bestFlavor.EqualsLiteral(kURLDataMime))) {
    AutoTransactionsConserveSelection dontChangeMySelection(*this);

    nsAutoString stuffToPaste;
    if (nsCOMPtr<nsISupportsString> text = do_QueryInterface(genericDataObj)) {
      text->GetData(stuffToPaste);
    }
    MOZ_ASSERT(GetEditAction() == EditAction::ePaste);
    // Use native line breaks for compatibility with Chrome.
    // XXX Although, somebody has already converted native line breaks to
    //     XP line breaks.
    UpdateEditActionData(stuffToPaste);

    nsresult rv = MaybeDispatchBeforeInputEvent();
    if (NS_FAILED(rv)) {
      NS_WARNING_ASSERTION(
          rv == NS_ERROR_EDITOR_ACTION_CANCELED,
          "EditorBase::MaybeDispatchBeforeInputEvent() failed");
      return rv;
    }

    if (!stuffToPaste.IsEmpty()) {
      // Sanitize possible carriage returns in the string to be inserted
      nsContentUtils::PlatformToDOMLineBreaks(stuffToPaste);

      AutoPlaceholderBatch treatAsOneTransaction(
          *this, ScrollSelectionIntoView::Yes, __FUNCTION__);
      nsresult rv =
          InsertTextAsSubAction(stuffToPaste, InsertTextFor::NormalText);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::InsertTextAsSubAction() failed");
        return rv;
      }
    }
  }

  // Try to scroll the selection into view if the paste/drop succeeded
  rv = ScrollSelectionFocusIntoView();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::ScrollSelectionFocusIntoView() failed");
  return rv;
}

nsresult TextEditor::InsertDroppedDataTransferAsAction(
    AutoEditActionDataSetter& aEditActionData, DataTransfer& aDataTransfer,
    const EditorDOMPoint& aDroppedAt, nsIPrincipal* /*aSourcePrincipal*/) {
  MOZ_ASSERT(aEditActionData.GetEditAction() == EditAction::eDrop);
  MOZ_ASSERT(GetEditAction() == EditAction::eDrop);
  MOZ_ASSERT(aDroppedAt.IsSet());
  MOZ_ASSERT(aDataTransfer.MozItemCount() > 0);

  uint32_t numItems = aDataTransfer.MozItemCount();
  AutoTArray<nsString, 5> textArray;
  textArray.SetCapacity(numItems);
  uint32_t textLength = 0;
  for (uint32_t i = 0; i < numItems; ++i) {
    nsCOMPtr<nsIVariant> data;
    aDataTransfer.GetDataAtNoSecurityCheck(u"text/plain"_ns, i,
                                           getter_AddRefs(data));
    if (!data) {
      continue;
    }
    // Use nsString to avoid copying its storage to textArray.
    nsString insertText;
    data->GetAsAString(insertText);
    if (insertText.IsEmpty()) {
      continue;
    }
    textArray.AppendElement(insertText);
    textLength += insertText.Length();
  }
  // Use nsString to avoid copying its storage to aEditActionData.
  nsString data;
  data.SetCapacity(textLength);
  // Join the text array from end to start because we insert each items
  // in the aDataTransfer at same point from start to end.  Although I
  // don't know whether this is intentional behavior.
  for (nsString& text : Reversed(textArray)) {
    data.Append(text);
  }
  // Use native line breaks for compatibility with Chrome.
  // XXX Although, somebody has already converted native line breaks to
  //     XP line breaks.
  aEditActionData.SetData(data);

  nsresult rv = aEditActionData.MaybeDispatchBeforeInputEvent();
  if (NS_FAILED(rv)) {
    NS_WARNING_ASSERTION(rv == NS_ERROR_EDITOR_ACTION_CANCELED,
                         "MaybeDispatchBeforeInputEvent() failed");
    return rv;
  }

  // Then, insert the text.  Note that we shouldn't need to walk the array
  // anymore because nobody should listen to mutation events of anonymous
  // text node in <input>/<textarea>.
  nsContentUtils::PlatformToDOMLineBreaks(data);
  rv = InsertTextAt(data, aDroppedAt, DeleteSelectedContent::No);
  if (NS_WARN_IF(Destroyed())) {
    return NS_ERROR_EDITOR_DESTROYED;
  }
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertTextAt(DeleteSelectedContent::No) "
                       "failed, but ignored");
  return rv;
}

nsresult TextEditor::HandlePaste(AutoEditActionDataSetter& /*aEditActionData*/,
                                 nsIClipboard::ClipboardType aClipboardType,
                                 DataTransfer* aDataTransfer) {
  if (NS_WARN_IF(!GetDocument())) {
    return NS_OK;
  }

  // The data will be initialized in InsertTextFromTransferable() if we're not
  // an HTMLEditor.  Therefore, we cannot dispatch "beforeinput" here.

  // Get Clipboard Service
  nsresult rv;
  nsCOMPtr<nsIClipboard> clipboard =
      do_GetService("@mozilla.org/widget/clipboard;1", &rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to get nsIClipboard service");
    return rv;
  }

  // Get the nsITransferable interface for getting the data from the clipboard
  Result<nsCOMPtr<nsITransferable>, nsresult> maybeTransferable =
      EditorUtils::CreateTransferableForPlainText(*GetDocument());
  if (maybeTransferable.isErr()) {
    NS_WARNING("EditorUtils::CreateTransferableForPlainText() failed");
    return maybeTransferable.unwrapErr();
  }
  nsCOMPtr<nsITransferable> transferable(maybeTransferable.unwrap());
  if (NS_WARN_IF(!transferable)) {
    NS_WARNING(
        "EditorUtils::CreateTransferableForPlainText() returned nullptr, but "
        "ignored");
    return NS_OK;  // XXX Why?
  }
  // Get the Data from the clipboard.
  rv = GetDataFromDataTransferOrClipboard(aDataTransfer, transferable,
                                          aClipboardType);
  if (NS_FAILED(rv)) {
    NS_WARNING("EditorBase::GetDataFromDataTransferOrClipboard() failed");
    return rv;
  }

  // XXX Why don't we check this first?
  if (!IsModifiable()) {
    return NS_OK;
  }
  rv = InsertTextFromTransferable(transferable);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "TextEditor::InsertTextFromTransferable() failed");
  return rv;
}

nsresult TextEditor::HandlePasteTransferable(
    AutoEditActionDataSetter& /*aEditActionData*/,
    nsITransferable& aTransferable, DataTransfer* /*aDataTransfer*/) {
  if (!IsModifiable()) {
    return NS_OK;
  }

  // FYI: The data of beforeinput will be initialized in
  // InsertTextFromTransferable().  Therefore, here does not touch
  // aEditActionData.
  nsresult rv = InsertTextFromTransferable(&aTransferable);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "TextEditor::InsertTextFromTransferable() failed");
  return rv;
}

bool TextEditor::CanPaste(nsIClipboard::ClipboardType aClipboardType) const {
  if (AreClipboardCommandsUnconditionallyEnabled()) {
    return true;
  }

  // can't paste if readonly
  if (!IsModifiable()) {
    return false;
  }

  nsresult rv;
  nsCOMPtr<nsIClipboard> clipboard(
      do_GetService("@mozilla.org/widget/clipboard;1", &rv));
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to get nsIClipboard service");
    return false;
  }

  // the flavors that we can deal with
  AutoTArray<nsCString, 2> textEditorFlavors = {
      nsDependentCString(kTextMime), nsDependentCString(kURLDataMime)};

  bool haveFlavors;
  rv = clipboard->HasDataMatchingFlavors(textEditorFlavors, aClipboardType,
                                         &haveFlavors);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "nsIClipboard::HasDataMatchingFlavors() failed");
  return NS_SUCCEEDED(rv) && haveFlavors;
}

bool TextEditor::CanPasteTransferable(nsITransferable* aTransferable) {
  // can't paste if readonly
  if (!IsModifiable()) {
    return false;
  }

  // If |aTransferable| is null, assume that a paste will succeed.
  if (!aTransferable) {
    return true;
  }

  nsCOMPtr<nsISupports> data;
  nsresult rv = aTransferable->GetTransferData(kTextMime, getter_AddRefs(data));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "nsITransferable::GetTransferData(kTextMime) failed");
  if (NS_SUCCEEDED(rv) && data) {
    return true;
  }
  rv = aTransferable->GetTransferData(kURLDataMime, getter_AddRefs(data));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "nsITransferable::GetTransferData(kURLDataMime) "
                       "failed");
  if (NS_SUCCEEDED(rv) && data) {
    return true;
  }
  return false;
}

}  // namespace mozilla
