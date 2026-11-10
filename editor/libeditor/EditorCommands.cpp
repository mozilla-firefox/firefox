/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EditorCommands.h"

#include "mozilla/Assertions.h"
#include "mozilla/EditorBase.h"
#include "mozilla/FlushType.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"  // for mozilla::detail::Any
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/UserActivation.h"
#include "nsCommandParams.h"
#include "nsIClipboard.h"
#include "nsIEditingSession.h"
#include "nsIPrincipal.h"
#include "nsISelectionController.h"
#include "nsITransferable.h"
#include "nsString.h"
#include "nsAString.h"

class nsISupports;

#define STATE_ENABLED "state_enabled"
#define STATE_ATTRIBUTE "state_attribute"
#define STATE_DATA "state_data"

namespace mozilla {

using detail::Any;

/******************************************************************************
 * mozilla::EditorCommand
 ******************************************************************************/

bool EditorCommand::IsCommandEnabled(const nsACString& aCommandName,
                                     nsISupports* aCommandRefCon) {
  nsCOMPtr<nsIEditor> editor = do_QueryInterface(aCommandRefCon);
  EditorBase* editorBase = editor ? editor->AsEditorBase() : nullptr;
  return IsCommandEnabled(GetInternalCommand(aCommandName),
                          MOZ_KnownLive(editorBase));
}

nsresult EditorCommand::DoCommand(const nsACString& aCommandName,
                                  nsICommandParams* aParams,
                                  nsISupports* aCommandRefCon) {
  if (NS_WARN_IF(!aCommandRefCon)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCOMPtr<nsIEditor> editor = do_QueryInterface(aCommandRefCon);
  if (NS_WARN_IF(!editor)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsCommandParams* params = aParams ? aParams->AsCommandParams() : nullptr;
  Command command = GetInternalCommand(aCommandName, params);
  EditorCommandParamType paramType = EditorCommand::GetParamType(command);
  if (paramType == EditorCommandParamType::None) {
    nsresult rv = DoCommandParam(
        command, MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "Failed to do command from nsIControllerCommand::DoCommandParams()");
    return rv;
  }

  if (Any(paramType & EditorCommandParamType::Bool)) {
    if (Any(paramType & EditorCommandParamType::StateAttribute)) {
      Maybe<bool> boolParam = Nothing();
      if (params) {
        ErrorResult error;
        boolParam = Some(params->GetBool(STATE_ATTRIBUTE, error));
        if (NS_WARN_IF(error.Failed())) {
          return error.StealNSResult();
        }
      }
      nsresult rv = DoCommandParam(
          command, boolParam, MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "Failed to do command from nsIControllerCommand::DoCommandParams()");
      return rv;
    }
    MOZ_ASSERT_UNREACHABLE("Unexpected state for bool");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // Special case for MultiStateCommandBase.  It allows both CString and String
  // in STATE_ATTRIBUTE and CString is preferred.
  if (Any(paramType & EditorCommandParamType::CString) &&
      Any(paramType & EditorCommandParamType::String)) {
    if (!params) {
      nsresult rv =
          DoCommandParam(command, VoidString(),
                         MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "Failed to do command from "
                           "nsIControllerCommand::DoCommandParams()");
      return rv;
    }
    if (Any(paramType & EditorCommandParamType::StateAttribute)) {
      nsCString cStringParam;
      nsresult rv = params->GetCString(STATE_ATTRIBUTE, cStringParam);
      if (NS_SUCCEEDED(rv)) {
        NS_ConvertUTF8toUTF16 stringParam(cStringParam);
        nsresult rv =
            DoCommandParam(command, stringParam,
                           MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "Failed to do command from "
                             "nsIControllerCommand::DoCommandParams()");
        return rv;
      }
      nsString stringParam;
      DebugOnly<nsresult> rvIgnored =
          params->GetString(STATE_ATTRIBUTE, stringParam);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "Failed to get string from STATE_ATTRIBUTE");
      rv = DoCommandParam(command, stringParam,
                          MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "Failed to do command from "
                           "nsIControllerCommand::DoCommandParams()");
      return rv;
    }
    MOZ_ASSERT_UNREACHABLE("Unexpected state for CString/String");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  if (Any(paramType & EditorCommandParamType::CString)) {
    if (!params) {
      nsresult rv =
          DoCommandParam(command, VoidCString(),
                         MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "Failed to do command from nsIControllerCommand::DoCommandParams()");
      return rv;
    }
    if (Any(paramType & EditorCommandParamType::StateAttribute)) {
      nsCString cStringParam;
      nsresult rv = params->GetCString(STATE_ATTRIBUTE, cStringParam);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      rv = DoCommandParam(command, cStringParam,
                          MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "Failed to do command from nsIControllerCommand::DoCommandParams()");
      return rv;
    }
    MOZ_ASSERT_UNREACHABLE("Unexpected state for CString");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  if (Any(paramType & EditorCommandParamType::String)) {
    if (!params) {
      nsresult rv =
          DoCommandParam(command, VoidString(),
                         MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "Failed to do command from nsIControllerCommand::DoCommandParams()");
      return rv;
    }
    nsString stringParam;
    if (Any(paramType & EditorCommandParamType::StateAttribute)) {
      nsresult rv = params->GetString(STATE_ATTRIBUTE, stringParam);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    } else if (Any(paramType & EditorCommandParamType::StateData)) {
      nsresult rv = params->GetString(STATE_DATA, stringParam);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    } else {
      MOZ_ASSERT_UNREACHABLE("Unexpected state for String");
      return NS_ERROR_NOT_IMPLEMENTED;
    }
    nsresult rv = DoCommandParam(
        command, stringParam, MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "Failed to do command from nsIControllerCommand::DoCommandParams()");
    return rv;
  }

  if (Any(paramType & EditorCommandParamType::Transferable)) {
    nsCOMPtr<nsITransferable> transferable;
    if (params) {
      nsCOMPtr<nsISupports> supports = params->GetISupports("transferable");
      transferable = do_QueryInterface(supports);
    }
    nsresult rv = DoCommandParam(
        command, transferable, MOZ_KnownLive(*editor->AsEditorBase()), nullptr);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "Failed to do command from nsIControllerCommand::DoCommandParams()");
    return rv;
  }

  MOZ_ASSERT_UNREACHABLE("Unexpected param type");
  return NS_ERROR_NOT_IMPLEMENTED;
}

void EditorCommand::GetCommandStateParams(const nsACString& aCommandName,
                                          nsICommandParams* aParams,
                                          nsISupports* aCommandRefCon) {
  MOZ_ASSERT(aParams);
  if (nsCOMPtr<nsIEditor> editor = do_QueryInterface(aCommandRefCon)) {
    GetCommandStateParams(GetInternalCommand(aCommandName),
                          MOZ_KnownLive(*aParams->AsCommandParams()),
                          MOZ_KnownLive(editor->AsEditorBase()), nullptr);
    return;
  }
  nsCOMPtr<nsIEditingSession> editingSession =
      do_QueryInterface(aCommandRefCon);
  GetCommandStateParams(GetInternalCommand(aCommandName),
                        MOZ_KnownLive(*aParams->AsCommandParams()), nullptr,
                        editingSession);
}

/******************************************************************************
 * mozilla::UndoCommand
 ******************************************************************************/

StaticRefPtr<UndoCommand> UndoCommand::sInstance;

bool UndoCommand::IsCommandEnabled(Command /*aCommand*/,
                                   EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsModifiable() && aEditorBase->IsSelectionEditable() &&
         aEditorBase->CanUndo();
}

nsresult UndoCommand::DoCommand(Command /*aCommand*/, EditorBase& aEditorBase,
                                nsIPrincipal* aPrincipal) const {
  nsresult rv = aEditorBase.UndoAsAction(1, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::UndoAsAction() failed");
  return rv;
}

nsresult UndoCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::RedoCommand
 ******************************************************************************/

StaticRefPtr<RedoCommand> RedoCommand::sInstance;

bool RedoCommand::IsCommandEnabled(Command /*aCommand*/,
                                   EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsModifiable() && aEditorBase->IsSelectionEditable() &&
         aEditorBase->CanRedo();
}

nsresult RedoCommand::DoCommand(Command /*aCommand*/, EditorBase& aEditorBase,
                                nsIPrincipal* aPrincipal) const {
  nsresult rv = aEditorBase.RedoAsAction(1, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::RedoAsAction() failed");
  return rv;
}

nsresult RedoCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::CutCommand
 ******************************************************************************/

StaticRefPtr<CutCommand> CutCommand::sInstance;

bool CutCommand::IsCommandEnabled(Command /*aCommand*/,
                                  EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsSelectionEditable() &&
         aEditorBase->IsCutCommandEnabled();
}

nsresult CutCommand::DoCommand(Command /*aCommand*/, EditorBase& aEditorBase,
                               nsIPrincipal* aPrincipal) const {
  nsresult rv = aEditorBase.CutAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::CutAsAction() failed");
  return rv;
}

nsresult CutCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::CutOrDeleteCommand
 ******************************************************************************/

StaticRefPtr<CutOrDeleteCommand> CutOrDeleteCommand::sInstance;

bool CutOrDeleteCommand::IsCommandEnabled(Command /*aCommand*/,
                                          EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsSelectionEditable();
}

nsresult CutOrDeleteCommand::DoCommand(Command /*aCommand*/,
                                       EditorBase& aEditorBase,
                                       nsIPrincipal* aPrincipal) const {
  dom::Selection* selection = aEditorBase.GetSelection();
  if (selection && selection->IsCollapsed()) {
    nsresult rv = aEditorBase.DeleteSelectionAsAction(
        nsIEditor::eNext, nsIEditor::eStrip, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::DeleteSelectionAsAction() failed");
    return rv;
  }
  nsresult rv = aEditorBase.CutAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::CutAsAction() failed");
  return rv;
}

nsresult CutOrDeleteCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::CopyCommand
 ******************************************************************************/

StaticRefPtr<CopyCommand> CopyCommand::sInstance;

bool CopyCommand::IsCommandEnabled(Command /*aCommand*/,
                                   EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsCopyCommandEnabled();
}

nsresult CopyCommand::DoCommand(Command /*aCommand*/, EditorBase& aEditorBase,
                                nsIPrincipal* /*aPrincipal*/) const {
  // Shouldn't cause "beforeinput" event so that we don't need to specify
  // the given principal.
  return aEditorBase.Copy();
}

nsresult CopyCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::CopyOrDeleteCommand
 ******************************************************************************/

StaticRefPtr<CopyOrDeleteCommand> CopyOrDeleteCommand::sInstance;

bool CopyOrDeleteCommand::IsCommandEnabled(Command /*aCommand*/,
                                           EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsSelectionEditable();
}

nsresult CopyOrDeleteCommand::DoCommand(Command /*aCommand*/,
                                        EditorBase& aEditorBase,
                                        nsIPrincipal* aPrincipal) const {
  dom::Selection* selection = aEditorBase.GetSelection();
  if (selection && selection->IsCollapsed()) {
    nsresult rv = aEditorBase.DeleteSelectionAsAction(
        nsIEditor::eNextWord, nsIEditor::eStrip, aPrincipal);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "EditorBase::DeleteSelectionAsAction() failed");
    return rv;
  }
  // Shouldn't cause "beforeinput" event so that we don't need to specify
  // the given principal.
  nsresult rv = aEditorBase.Copy();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::Copy() failed");
  return rv;
}

nsresult CopyOrDeleteCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::PasteCommand
 ******************************************************************************/

StaticRefPtr<PasteCommand> PasteCommand::sInstance;

bool PasteCommand::IsCommandEnabled(Command /*aCommand*/,
                                    EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsSelectionEditable() &&
         aEditorBase->CanPaste(nsIClipboard::kGlobalClipboard);
}

nsresult PasteCommand::DoCommand(Command /*aCommand*/, EditorBase& aEditorBase,
                                 nsIPrincipal* aPrincipal) const {
  RefPtr<dom::DataTransfer> dataTransfer;
  nsCOMPtr<nsIPrincipal> subjectPrincipal =
      aPrincipal ? aPrincipal
                 : nsContentUtils::SubjectPrincipalOrSystemIfNativeCaller();
  MOZ_ASSERT(subjectPrincipal);

  // If we don't need to get user confirmation for clipboard access, we could
  // just let EditorBase::PasteAsAction() to create DataTransfer instance
  // synchronously for paste event. Otherwise, we need to spin the event loop to
  // wait for the clipboard paste contextmenu to be shown and get user
  // confirmation which are all handled in parent process before sending the
  // paste event.
  if (!nsContentUtils::PrincipalHasPermission(*subjectPrincipal,
                                              nsGkAtoms::clipboardRead) &&
      !dom::UserActivation::IsHandlingKeyboardInputWithPasteActions()) {
    MOZ_DIAGNOSTIC_ASSERT(StaticPrefs::dom_execCommand_paste_enabled(),
                          "How did we get here?");
    // This will spin the event loop.
    nsCOMPtr<nsPIDOMWindowOuter> window = aEditorBase.GetWindow();
    dataTransfer = dom::DataTransfer::WaitForClipboardDataSnapshotAndCreate(
        window, subjectPrincipal);
    if (!dataTransfer) {
      return NS_SUCCESS_DOM_NO_OPERATION;
    }
  }

  nsresult rv = aEditorBase.PasteAsAction(nsIClipboard::kGlobalClipboard,
                                          EditorBase::DispatchPasteEvent::Yes,
                                          dataTransfer, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::PasteAsAction(nsIClipboard::"
                       "kGlobalClipboard, DispatchPasteEvent::Yes) failed");
  return rv;
}

nsresult PasteCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/*****************************************************************************
 * mozilla::PasteNoFormattingCommand
 *****************************************************************************/

StaticRefPtr<PasteNoFormattingCommand> PasteNoFormattingCommand::sInstance;

bool PasteNoFormattingCommand::IsCommandEnabled(Command /*aCommand*/,
                                                EditorBase* aEditorBase) const {
  return aEditorBase && aEditorBase->IsSelectionEditable() &&
         aEditorBase->CanPaste(nsIClipboard::kGlobalClipboard);
}

nsresult PasteNoFormattingCommand::DoCommand(Command /*aCommand*/,
                                             EditorBase& aEditorBase,
                                             nsIPrincipal* aPrincipal) const {
#ifdef DEBUG
  // cmd_pasteNoFormatting is not available through document.execCommand.
  // So it should always have clipboardRead permission.
  nsCOMPtr<nsIPrincipal> subjectPrincipal =
      aPrincipal ? aPrincipal
                 : nsContentUtils::SubjectPrincipalOrSystemIfNativeCaller();
  MOZ_ASSERT(nsContentUtils::PrincipalHasPermission(*subjectPrincipal,
                                                    nsGkAtoms::clipboardRead));
#endif
  nsresult rv = aEditorBase.PasteNoFormattingAsAction(
      nsIClipboard::kGlobalClipboard, EditorBase::DispatchPasteEvent::Yes,
      nullptr, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::PasteNoFormattingAsAction(nsIClipboard::"
                       "kGlobalClipboard, DispatchPasteEvent::Yes) failed");
  return rv;
}

nsresult PasteNoFormattingCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::PasteTransferableCommand
 ******************************************************************************/

StaticRefPtr<PasteTransferableCommand> PasteTransferableCommand::sInstance;

bool PasteTransferableCommand::IsCommandEnabled(Command /*aCommand*/,
                                                EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsSelectionEditable() &&
         aEditorBase->CanPasteTransferable(nullptr);
}

nsresult PasteTransferableCommand::DoCommand(
    Command /*aCommand*/, EditorBase& /*aEditorBase*/,
    nsIPrincipal* /*aPrincipal*/) const {
  return NS_ERROR_FAILURE;
}

nsresult PasteTransferableCommand::DoCommandParam(
    Command /*aCommand*/, nsITransferable* aTransferableParam,
    EditorBase& aEditorBase, nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(!aTransferableParam)) {
    return NS_ERROR_INVALID_ARG;
  }
  nsresult rv = aEditorBase.PasteTransferableAsAction(
      aTransferableParam, EditorBase::DispatchPasteEvent::Yes, aPrincipal);
  NS_WARNING_ASSERTION(
      NS_SUCCEEDED(rv),
      "EditorBase::PasteTransferableAsAction(DispatchPasteEvent::Yes) failed");
  return rv;
}

nsresult PasteTransferableCommand::GetCommandStateParams(
    Command /*aCommand*/, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  if (NS_WARN_IF(!aEditorBase)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsISupports> supports = aParams.GetISupports("transferable");
  if (NS_WARN_IF(!supports)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsITransferable> trans;
  trans = do_QueryInterface(supports);
  if (NS_WARN_IF(!trans)) {
    return NS_ERROR_FAILURE;
  }

  return aParams.SetBool(STATE_ENABLED,
                         aEditorBase->CanPasteTransferable(trans));
}

/******************************************************************************
 * mozilla::SwitchTextDirectionCommand
 ******************************************************************************/

StaticRefPtr<SwitchTextDirectionCommand> SwitchTextDirectionCommand::sInstance;

bool SwitchTextDirectionCommand::IsCommandEnabled(
    Command /*aCommand*/, EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsModifiable() && aEditorBase->IsSelectionEditable();
}

nsresult SwitchTextDirectionCommand::DoCommand(Command /*aCommand*/,
                                               EditorBase& aEditorBase,
                                               nsIPrincipal* aPrincipal) const {
  nsresult rv = aEditorBase.ToggleTextDirectionAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::ToggleTextDirectionAsAction() failed");
  return rv;
}

nsresult SwitchTextDirectionCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::DeleteCommand
 ******************************************************************************/

StaticRefPtr<DeleteCommand> DeleteCommand::sInstance;

bool DeleteCommand::IsCommandEnabled(Command aCommand,
                                     EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  // We can generally delete whenever the selection is editable.  However,
  // cmd_delete doesn't make sense if the selection is collapsed because it's
  // directionless.
  bool isEnabled =
      aEditorBase->IsModifiable() && aEditorBase->IsSelectionEditable();

  if (aCommand == Command::Delete && isEnabled) {
    return aEditorBase->CanDeleteSelection();
  }
  return isEnabled;
}

nsresult DeleteCommand::DoCommand(Command aCommand, EditorBase& aEditorBase,
                                  nsIPrincipal* aPrincipal) const {
  nsIEditor::EDirection deleteDir = nsIEditor::eNone;
  switch (aCommand) {
    case Command::Delete:
      // Really this should probably be eNone, but it only makes a difference
      // if the selection is collapsed, and then this command is disabled.  So
      // let's keep it as it always was to avoid breaking things.
      deleteDir = nsIEditor::ePrevious;
      break;
    case Command::DeleteCharForward:
      deleteDir = nsIEditor::eNext;
      break;
    case Command::DeleteCharBackward:
      deleteDir = nsIEditor::ePrevious;
      break;
    case Command::DeleteWordBackward:
      deleteDir = nsIEditor::ePreviousWord;
      break;
    case Command::DeleteWordForward:
      deleteDir = nsIEditor::eNextWord;
      break;
    case Command::DeleteToBeginningOfLine:
      deleteDir = nsIEditor::eToBeginningOfLine;
      break;
    case Command::DeleteToEndOfLine:
      deleteDir = nsIEditor::eToEndOfLine;
      break;
    default:
      MOZ_CRASH("Unrecognized nsDeleteCommand");
  }
  nsresult rv = aEditorBase.DeleteSelectionAsAction(
      deleteDir, nsIEditor::eStrip, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::DeleteSelectionAsAction() failed");
  return rv;
}

nsresult DeleteCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::SelectAllCommand
 ******************************************************************************/

StaticRefPtr<SelectAllCommand> SelectAllCommand::sInstance;

bool SelectAllCommand::IsCommandEnabled(Command /*aCommand*/,
                                        EditorBase* aEditorBase) const {
  // You can always select all, unless the selection is editable,
  // and the editable region is empty!
  if (!aEditorBase) {
    return true;
  }

  // You can select all if there is an editor which is non-empty
  return !aEditorBase->IsEmpty();
}

nsresult SelectAllCommand::DoCommand(Command /*aCommand*/,
                                     EditorBase& aEditorBase,
                                     nsIPrincipal* /*aPrincipal*/) const {
  // Shouldn't cause "beforeinput" event so that we don't need to specify
  // aPrincipal.
  nsresult rv = aEditorBase.SelectAll();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "EditorBase::SelectAll() failed");
  return rv;
}

nsresult SelectAllCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::SelectionMoveCommands
 ******************************************************************************/

StaticRefPtr<SelectionMoveCommands> SelectionMoveCommands::sInstance;

bool SelectionMoveCommands::IsCommandEnabled(Command /*aCommand*/,
                                             EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsSelectionEditable();
}

static const struct ScrollCommand {
  Command mReverseScroll;
  Command mForwardScroll;
  nsresult (NS_STDCALL nsISelectionController::*scroll)(bool);
} scrollCommands[] = {{Command::ScrollTop, Command::ScrollBottom,
                       &nsISelectionController::CompleteScroll},
                      {Command::ScrollPageUp, Command::ScrollPageDown,
                       &nsISelectionController::ScrollPage},
                      {Command::ScrollLineUp, Command::ScrollLineDown,
                       &nsISelectionController::ScrollLine}};

static const struct MoveCommand {
  Command mReverseMove;
  Command mForwardMove;
  Command mReverseSelect;
  Command mForwardSelect;
  nsresult (NS_STDCALL nsISelectionController::*move)(bool, bool);
} moveCommands[] = {
    {Command::CharPrevious, Command::CharNext, Command::SelectCharPrevious,
     Command::SelectCharNext, &nsISelectionController::CharacterMove},
    {Command::LinePrevious, Command::LineNext, Command::SelectLinePrevious,
     Command::SelectLineNext, &nsISelectionController::LineMove},
    {Command::WordPrevious, Command::WordNext, Command::SelectWordPrevious,
     Command::SelectWordNext, &nsISelectionController::WordMove},
    {Command::BeginLine, Command::EndLine, Command::SelectBeginLine,
     Command::SelectEndLine, &nsISelectionController::IntraLineMove},
    {Command::BeginParagraph, Command::EndParagraph,
     Command::SelectBeginParagraph, Command::SelectEndParagraph,
     &nsISelectionController::ParagraphMove},
    {Command::MovePageUp, Command::MovePageDown, Command::SelectPageUp,
     Command::SelectPageDown, &nsISelectionController::PageMove},
    {Command::MoveTop, Command::MoveBottom, Command::SelectTop,
     Command::SelectBottom, &nsISelectionController::CompleteMove}};

static const struct PhysicalCommand {
  Command mMove;
  Command mSelect;
  int16_t direction;
  int16_t amount;
} physicalCommands[] = {
    {Command::MoveLeft, Command::SelectLeft, nsISelectionController::MOVE_LEFT,
     0},
    {Command::MoveRight, Command::SelectRight,
     nsISelectionController::MOVE_RIGHT, 0},
    {Command::MoveUp, Command::SelectUp, nsISelectionController::MOVE_UP, 0},
    {Command::MoveDown, Command::SelectDown, nsISelectionController::MOVE_DOWN,
     0},
    {Command::MoveLeft2, Command::SelectLeft2,
     nsISelectionController::MOVE_LEFT, 1},
    {Command::MoveRight2, Command::SelectRight2,
     nsISelectionController::MOVE_RIGHT, 1},
    {Command::MoveUp2, Command::SelectUp2, nsISelectionController::MOVE_UP, 1},
    {Command::MoveDown2, Command::SelectDown2,
     nsISelectionController::MOVE_DOWN, 1},
    {Command::MoveLeft3, Command::SelectLeft3,
     nsISelectionController::MOVE_LEFT, 2},
    {Command::MoveRight3, Command::SelectRight3,
     nsISelectionController::MOVE_RIGHT, 2}};

nsresult SelectionMoveCommands::DoCommand(Command aCommand,
                                          EditorBase& aEditorBase,
                                          nsIPrincipal* /*aPrincipal*/) const {
  RefPtr<dom::Document> document = aEditorBase.GetDocument();
  if (document) {
    // Most of the commands below (possibly all of them) need layout to
    // be up to date.
    document->FlushPendingNotifications(FlushType::Layout);
  }

  nsCOMPtr<nsISelectionController> selectionController =
      aEditorBase.GetSelectionController();
  if (NS_WARN_IF(!selectionController)) {
    return NS_ERROR_FAILURE;
  }

  // scroll commands
  for (const auto& cmd : scrollCommands) {
    if (aCommand == cmd.mReverseScroll) {
      return (selectionController->*(cmd.scroll))(false);
    }
    if (aCommand == cmd.mForwardScroll) {
      return (selectionController->*(cmd.scroll))(true);
    }
  }

  // caret movement/selection commands
  for (const auto& cmd : moveCommands) {
    if (aCommand == cmd.mReverseMove) {
      return (selectionController->*(cmd.move))(false, false);
    }
    if (aCommand == cmd.mForwardMove) {
      return (selectionController->*(cmd.move))(true, false);
    }
    if (aCommand == cmd.mReverseSelect) {
      return (selectionController->*(cmd.move))(false, true);
    }
    if (aCommand == cmd.mForwardSelect) {
      return (selectionController->*(cmd.move))(true, true);
    }
  }

  // physical-direction movement/selection
  for (auto cmd : physicalCommands) {
    if (aCommand == cmd.mMove) {
      nsresult rv =
          selectionController->PhysicalMove(cmd.direction, cmd.amount, false);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "nsISelectionController::PhysicalMove() failed to move caret");
      return rv;
    }
    if (aCommand == cmd.mSelect) {
      nsresult rv =
          selectionController->PhysicalMove(cmd.direction, cmd.amount, true);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "nsISelectionController::PhysicalMove() failed to select");
      return rv;
    }
  }

  return NS_ERROR_FAILURE;
}

nsresult SelectionMoveCommands::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::InsertPlaintextCommand
 ******************************************************************************/

StaticRefPtr<InsertPlaintextCommand> InsertPlaintextCommand::sInstance;

bool InsertPlaintextCommand::IsCommandEnabled(Command /*aCommand*/,
                                              EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return aEditorBase->IsModifiable() && aEditorBase->IsSelectionEditable();
}

nsresult InsertPlaintextCommand::DoCommand(Command /*aCommand*/,
                                           EditorBase& aEditorBase,
                                           nsIPrincipal* aPrincipal) const {
  // XXX InsertTextAsAction() is not same as OnInputText().  However, other
  //     commands to insert line break or paragraph separator use OnInput*().
  //     According to the semantics of those methods, using *AsAction() is
  //     better, however, this may not cause two or more placeholder
  //     transactions to the top transaction since its name may not be
  //     nsGkAtoms::TypingTxnName.
  DebugOnly<nsresult> rvIgnored =
      aEditorBase.InsertTextAsAction(u""_ns, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::InsertTextAsAction() failed, but ignored");
  return NS_OK;
}

nsresult InsertPlaintextCommand::DoCommandParam(
    Command /*aCommand*/, const nsAString& aStringParam,
    EditorBase& aEditorBase, nsIPrincipal* aPrincipal) const {
  if (NS_WARN_IF(aStringParam.IsVoid())) {
    return NS_ERROR_INVALID_ARG;
  }

  // XXX InsertTextAsAction() is not same as OnInputText().  However, other
  //     commands to insert line break or paragraph separator use OnInput*().
  //     According to the semantics of those methods, using *AsAction() is
  //     better, however, this may not cause two or more placeholder
  //     transactions to the top transaction since its name may not be
  //     nsGkAtoms::TypingTxnName.
  DebugOnly<nsresult> rvIgnored =
      aEditorBase.InsertTextAsAction(aStringParam, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                       "EditorBase::InsertTextAsAction() failed, but ignored");
  return NS_OK;
}

nsresult InsertPlaintextCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::InsertParagraphCommand
 ******************************************************************************/

StaticRefPtr<InsertParagraphCommand> InsertParagraphCommand::sInstance;

bool InsertParagraphCommand::IsCommandEnabled(Command /*aCommand*/,
                                              EditorBase* aEditorBase) const {
  if (!aEditorBase || aEditorBase->IsSingleLineEditor()) {
    return false;
  }
  return aEditorBase->IsModifiable() && aEditorBase->IsSelectionEditable();
}

nsresult InsertParagraphCommand::DoCommand(Command /*aCommand*/,
                                           EditorBase& aEditorBase,
                                           nsIPrincipal* aPrincipal) const {
  if (aEditorBase.IsSingleLineEditor()) {
    return NS_ERROR_FAILURE;
  }
  if (aEditorBase.IsHTMLEditor()) {
    nsresult rv = MOZ_KnownLive(aEditorBase.AsHTMLEditor())
                      ->InsertParagraphSeparatorAsAction(aPrincipal);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "HTMLEditor::InsertParagraphSeparatorAsAction() failed");
    return rv;
  }
  nsresult rv = aEditorBase.InsertLineBreakAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertLineBreakAsAction() failed");
  return rv;
}

nsresult InsertParagraphCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::InsertLineBreakCommand
 ******************************************************************************/

StaticRefPtr<InsertLineBreakCommand> InsertLineBreakCommand::sInstance;

bool InsertLineBreakCommand::IsCommandEnabled(Command /*aCommand*/,
                                              EditorBase* aEditorBase) const {
  if (!aEditorBase || aEditorBase->IsSingleLineEditor()) {
    return false;
  }
  return aEditorBase->IsModifiable() && aEditorBase->IsSelectionEditable();
}

nsresult InsertLineBreakCommand::DoCommand(Command /*aCommand*/,
                                           EditorBase& aEditorBase,
                                           nsIPrincipal* aPrincipal) const {
  if (aEditorBase.IsSingleLineEditor()) {
    return NS_ERROR_FAILURE;
  }
  nsresult rv = aEditorBase.InsertLineBreakAsAction(aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::InsertLineBreakAsAction() failed");
  return rv;
}

nsresult InsertLineBreakCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  return aParams.SetBool(STATE_ENABLED,
                         IsCommandEnabled(aCommand, aEditorBase));
}

/******************************************************************************
 * mozilla::PasteQuotationCommand
 ******************************************************************************/

StaticRefPtr<PasteQuotationCommand> PasteQuotationCommand::sInstance;

bool PasteQuotationCommand::IsCommandEnabled(Command /*aCommand*/,
                                             EditorBase* aEditorBase) const {
  if (!aEditorBase) {
    return false;
  }
  return !aEditorBase->IsSingleLineEditor() &&
         aEditorBase->CanPaste(nsIClipboard::kGlobalClipboard);
}

nsresult PasteQuotationCommand::DoCommand(Command /*aCommand*/,
                                          EditorBase& aEditorBase,
                                          nsIPrincipal* aPrincipal) const {
  nsresult rv = aEditorBase.PasteAsQuotationAsAction(
      nsIClipboard::kGlobalClipboard, EditorBase::DispatchPasteEvent::Yes,
      nullptr, aPrincipal);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "EditorBase::PasteAsQuotationAsAction(nsIClipboard::"
                       "kGlobalClipboard, DispatchPasteEvent::Yes) failed");
  return rv;
}

nsresult PasteQuotationCommand::GetCommandStateParams(
    Command /*aCommand*/, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  if (!aEditorBase) {
    return NS_OK;
  }
  aParams.SetBool(STATE_ENABLED,
                  aEditorBase->CanPaste(nsIClipboard::kGlobalClipboard));
  return NS_OK;
}

}  // namespace mozilla
