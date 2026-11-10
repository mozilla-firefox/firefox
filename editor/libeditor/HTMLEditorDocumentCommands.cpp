/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EditorCommands.h"

#include "EditorBase.h"  // for EditorBase
#include "ErrorList.h"
#include "HTMLEditor.h"  // for HTMLEditor

#include "mozilla/BasePrincipal.h"  // for nsIPrincipal::IsSystemPrincipal()
#include "mozilla/dom/Element.h"    // for Element
#include "mozilla/dom/Document.h"   // for Document
#include "mozilla/dom/HTMLInputElement.h"     // for HTMLInputElement
#include "mozilla/dom/HTMLTextAreaElement.h"  // for HTMLTextAreaElement

#include "nsCommandParams.h"    // for nsCommandParams
#include "nsIEditingSession.h"  // for nsIEditingSession, etc
#include "nsIPrincipal.h"       // for nsIPrincipal
#include "nsISupportsImpl.h"    // for nsPresContext::Release
#include "nsISupportsUtils.h"   // for NS_IF_ADDREF
#include "nsIURI.h"             // for nsIURI
#include "nsPresContext.h"      // for nsPresContext

// defines
#define STATE_ENABLED "state_enabled"
#define STATE_ALL "state_all"
#define STATE_ATTRIBUTE "state_attribute"
#define STATE_DATA "state_data"

namespace mozilla {

using namespace dom;

/*****************************************************************************
 * mozilla::SetDocumentStateCommand
 *
 *  Commands for document state that may be changed via doCommandParams
 *  As of 11/11/02, this is just "cmd_setDocumentModified"
 *  Note that you can use the same command class, SetDocumentStateCommand,
 *    for more than one of this type of command
 *    We check the input command param for different behavior
 *****************************************************************************/

StaticRefPtr<SetDocumentStateCommand> SetDocumentStateCommand::sInstance;

bool SetDocumentStateCommand::IsCommandEnabled(Command aCommand,
                                               EditorBase* aEditorBase) const {
  switch (aCommand) {
    case Command::SetDocumentReadOnly:
      return !!aEditorBase;
    default:
      // The other commands are always enabled if given editor is an HTMLEditor.
      return aEditorBase && aEditorBase->IsHTMLEditor();
  }
}

nsresult SetDocumentStateCommand::DoCommand(
    Command /*aCommand*/, EditorBase& /*aEditorBase*/,
    nsIPrincipal* /*aPrincipal*/) const {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult SetDocumentStateCommand::DoCommandParam(
    Command aCommand, const Maybe<bool>& aBoolParam, EditorBase& aEditorBase,
    nsIPrincipal* /*aPrincipal*/) const {
  if (NS_WARN_IF(aBoolParam.isNothing())) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aCommand != Command::SetDocumentReadOnly &&
      NS_WARN_IF(!aEditorBase.IsHTMLEditor())) {
    return NS_ERROR_FAILURE;
  }

  switch (aCommand) {
    case Command::SetDocumentModified: {
      if (aBoolParam.value()) {
        nsresult rv = aEditorBase.IncrementModificationCount(1);
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "EditorBase::IncrementModificationCount() failed");
        return rv;
      }
      nsresult rv = aEditorBase.ResetModificationCount();
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "EditorBase::ResetModificationCount() failed");
      return rv;
    }
    case Command::SetDocumentReadOnly: {
      if (aEditorBase.IsTextEditor()) {
        Element* inputOrTextArea = aEditorBase.GetExposedRoot();
        if (NS_WARN_IF(!inputOrTextArea)) {
          return NS_ERROR_FAILURE;
        }
        // Perhaps, this legacy command shouldn't work with
        // `<input type="file">` and `<input type="number">.
        if (inputOrTextArea->IsInNativeAnonymousSubtree()) {
          return NS_ERROR_FAILURE;
        }
        if (RefPtr<HTMLInputElement> inputElement =
                HTMLInputElement::FromNode(inputOrTextArea)) {
          if (inputElement->ReadOnly() == aBoolParam.value()) {
            return NS_SUCCESS_DOM_NO_OPERATION;
          }
          ErrorResult error;
          inputElement->SetReadOnly(aBoolParam.value(), error);
          return error.StealNSResult();
        }
        if (RefPtr<HTMLTextAreaElement> textAreaElement =
                HTMLTextAreaElement::FromNode(inputOrTextArea)) {
          if (textAreaElement->ReadOnly() == aBoolParam.value()) {
            return NS_SUCCESS_DOM_NO_OPERATION;
          }
          ErrorResult error;
          textAreaElement->SetReadOnly(aBoolParam.value(), error);
          return error.StealNSResult();
        }
        NS_ASSERTION(
            false,
            "Unexpected exposed root element, fallthrough to directly make the "
            "editor readonly");
      }
      ErrorResult error;
      if (aBoolParam.value()) {
        nsresult rv = aEditorBase.AddFlags(nsIEditor::eEditorReadonlyMask);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rv),
            "EditorBase::AddFlags(nsIEditor::eEditorReadonlyMask) failed");
        return rv;
      }
      nsresult rv = aEditorBase.RemoveFlags(nsIEditor::eEditorReadonlyMask);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "EditorBase::RemoveFlags(nsIEditor::eEditorReadonlyMask) failed");
      return rv;
    }
    case Command::SetDocumentUseCSS: {
      nsresult rv = MOZ_KnownLive(aEditorBase.AsHTMLEditor())
                        ->SetIsCSSEnabled(aBoolParam.value());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "HTMLEditor::SetIsCSSEnabled() failed");
      return rv;
    }
    case Command::SetDocumentInsertBROnEnterKeyPress: {
      nsresult rv =
          aEditorBase.AsHTMLEditor()->SetReturnInParagraphCreatesNewParagraph(
              !aBoolParam.value());
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rv),
          "HTMLEditor::SetReturnInParagraphCreatesNewParagraph() failed");
      return rv;
    }
    case Command::ToggleObjectResizers: {
      MOZ_KnownLive(aEditorBase.AsHTMLEditor())
          ->EnableObjectResizer(aBoolParam.value());
      return NS_OK;
    }
    case Command::ToggleInlineTableEditor: {
      MOZ_KnownLive(aEditorBase.AsHTMLEditor())
          ->EnableInlineTableEditor(aBoolParam.value());
      return NS_OK;
    }
    case Command::ToggleAbsolutePositionEditor: {
      MOZ_KnownLive(aEditorBase.AsHTMLEditor())
          ->EnableAbsolutePositionEditor(aBoolParam.value());
      return NS_OK;
    }
    case Command::EnableCompatibleJoinSplitNodeDirection:
      // Now we don't support the legacy join/split node direction anymore, but
      // this result may be used for the feature detection whether Gecko
      // supports the new direction mode.  Therefore, even though we do nothing,
      // but we should return NS_OK to return `true` from
      // `Document.execCommand()`.
      return NS_OK;
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }
}

nsresult SetDocumentStateCommand::DoCommandParam(
    Command aCommand, const nsACString& aCStringParam, EditorBase& aEditorBase,
    nsIPrincipal* /*aPrincipal*/) const {
  if (NS_WARN_IF(aCStringParam.IsVoid())) {
    return NS_ERROR_INVALID_ARG;
  }

  if (NS_WARN_IF(!aEditorBase.IsHTMLEditor())) {
    return NS_ERROR_FAILURE;
  }

  switch (aCommand) {
    case Command::SetDocumentDefaultParagraphSeparator: {
      if (aCStringParam.LowerCaseEqualsLiteral("div")) {
        aEditorBase.AsHTMLEditor()->SetDefaultParagraphSeparator(
            ParagraphSeparator::div);
        return NS_OK;
      }
      if (aCStringParam.LowerCaseEqualsLiteral("p")) {
        aEditorBase.AsHTMLEditor()->SetDefaultParagraphSeparator(
            ParagraphSeparator::p);
        return NS_OK;
      }
      if (aCStringParam.LowerCaseEqualsLiteral("br")) {
        // Mozilla extension for backwards compatibility
        aEditorBase.AsHTMLEditor()->SetDefaultParagraphSeparator(
            ParagraphSeparator::br);
        return NS_OK;
      }

      // This should not be reachable from nsHTMLDocument::ExecCommand
      // XXX Shouldn't return error in this case because Chrome does not throw
      //     exception in this case.
      NS_WARNING("Invalid default paragraph separator");
      return NS_ERROR_UNEXPECTED;
    }
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }
}

nsresult SetDocumentStateCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* /*aEditingSession*/) const {
  // If the result is set to STATE_ATTRIBUTE as CString value,
  // queryCommandValue() returns the string value.
  // Otherwise, ignored.

  // The base editor owns most state info
  if (NS_WARN_IF(!aEditorBase)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (NS_WARN_IF(!aEditorBase->IsHTMLEditor())) {
    return NS_ERROR_FAILURE;
  }

  // Always get the enabled state
  nsresult rv =
      aParams.SetBool(STATE_ENABLED, IsCommandEnabled(aCommand, aEditorBase));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  switch (aCommand) {
    case Command::SetDocumentModified: {
      bool modified;
      rv = aEditorBase->GetDocumentModified(&modified);
      if (NS_FAILED(rv)) {
        NS_WARNING("EditorBase::GetDocumentModified() failed");
        return rv;
      }
      // XXX Nobody refers this result due to wrong type.
      rv = aParams.SetBool(STATE_ATTRIBUTE, modified);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "nsCommandParams::SetBool(STATE_ATTRIBUTE) failed");
      return rv;
    }
    case Command::SetDocumentReadOnly: {
      // XXX Nobody refers this result due to wrong type.
      rv = aParams.SetBool(STATE_ATTRIBUTE, aEditorBase->IsReadonly());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "nsCommandParams::SetBool(STATE_ATTRIBUTE) failed");
      return rv;
    }
    case Command::SetDocumentUseCSS: {
      HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
      if (NS_WARN_IF(!htmlEditor)) {
        return NS_ERROR_INVALID_ARG;
      }
      rv = aParams.SetBool(STATE_ALL, htmlEditor->IsCSSEnabled());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "nsCommandParams::SetBool(STATE_ALL) failed");
      return rv;
    }
    case Command::SetDocumentInsertBROnEnterKeyPress: {
      HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
      if (NS_WARN_IF(!htmlEditor)) {
        return NS_ERROR_INVALID_ARG;
      }
      bool createPOnReturn;
      DebugOnly<nsresult> rvIgnored =
          htmlEditor->GetReturnInParagraphCreatesNewParagraph(&createPOnReturn);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "HTMLEditor::GetReturnInParagraphCreatesNewParagraph() failed");
      // XXX Nobody refers this result due to wrong type.
      rv = aParams.SetBool(STATE_ATTRIBUTE, !createPOnReturn);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "nsCommandParams::SetBool(STATE_ATTRIBUTE) failed");
      return rv;
    }
    case Command::SetDocumentDefaultParagraphSeparator: {
      HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
      if (NS_WARN_IF(!htmlEditor)) {
        return NS_ERROR_INVALID_ARG;
      }

      switch (htmlEditor->GetDefaultParagraphSeparator()) {
        case ParagraphSeparator::div: {
          DebugOnly<nsresult> rv =
              aParams.SetCString(STATE_ATTRIBUTE, "div"_ns);
          NS_WARNING_ASSERTION(
              NS_SUCCEEDED(rv),
              "Failed to set command params to return \"div\"");
          return NS_OK;
        }
        case ParagraphSeparator::p: {
          DebugOnly<nsresult> rv = aParams.SetCString(STATE_ATTRIBUTE, "p"_ns);
          NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                               "Failed to set command params to return \"p\"");
          return NS_OK;
        }
        case ParagraphSeparator::br: {
          DebugOnly<nsresult> rv = aParams.SetCString(STATE_ATTRIBUTE, "br"_ns);
          NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                               "Failed to set command params to return \"br\"");
          return NS_OK;
        }
        default:
          MOZ_ASSERT_UNREACHABLE("Invalid paragraph separator value");
          return NS_ERROR_UNEXPECTED;
      }
    }
    case Command::ToggleObjectResizers: {
      HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
      if (NS_WARN_IF(!htmlEditor)) {
        return NS_ERROR_INVALID_ARG;
      }
      // We returned the result as STATE_ATTRIBUTE with bool value 60 or
      // earlier. So, the result was ignored by both
      // nsHTMLDocument::QueryCommandValue() and
      // nsHTMLDocument::QueryCommandState().
      rv = aParams.SetBool(STATE_ALL, htmlEditor->IsObjectResizerEnabled());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "nsCommandParams::SetBool(STATE_ALL) failed");
      return rv;
    }
    case Command::ToggleInlineTableEditor: {
      HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
      if (NS_WARN_IF(!htmlEditor)) {
        return NS_ERROR_INVALID_ARG;
      }
      // We returned the result as STATE_ATTRIBUTE with bool value 60 or
      // earlier. So, the result was ignored by both
      // nsHTMLDocument::QueryCommandValue() and
      // nsHTMLDocument::QueryCommandState().
      rv = aParams.SetBool(STATE_ALL, htmlEditor->IsInlineTableEditorEnabled());
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "nsCommandParams::SetBool(STATE_ALL) failed");
      return rv;
    }
    case Command::ToggleAbsolutePositionEditor: {
      HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
      if (NS_WARN_IF(!htmlEditor)) {
        return NS_ERROR_INVALID_ARG;
      }
      return aParams.SetBool(STATE_ALL,
                             htmlEditor->IsAbsolutePositionEditorEnabled());
    }
    case Command::EnableCompatibleJoinSplitNodeDirection: {
      HTMLEditor* htmlEditor = aEditorBase->GetAsHTMLEditor();
      if (NS_WARN_IF(!htmlEditor)) {
        return NS_ERROR_INVALID_ARG;
      }
      // Now we don't support the legacy join/split node direction anymore, but
      // this result may be used for the feature detection whether Gecko
      // supports the new direction mode.  Therefore, we should return `true`
      // even though executing the command does nothing.
      return aParams.SetBool(STATE_ALL, true);
    }
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }
}

/*****************************************************************************
 * mozilla::DocumentStateCommand
 *
 * Commands just for state notification
 *  As of 11/21/02, possible commands are:
 *    "obs_documentCreated"
 *    "obs_documentWillBeDestroyed"
 *    "obs_documentLocationChanged"
 *  Note that you can use the same command class, DocumentStateCommand
 *    for these or future observer commands.
 *    We check the input command param for different behavior
 *
 *  How to use:
 *  1. Get the nsCommandManager for the current editor
 *  2. Implement an nsIObserve object, e.g:
 *
 *    void Observe(
 *        in nsISupports aSubject, // The nsCommandManager calling this
 *                                 // Observer
 *        in string      aTopic,   // command name, e.g.:"obs_documentCreated"
 *                                 //    or "obs_documentWillBeDestroyed"
          in wstring     aData );  // ignored (set to "command_status_changed")
 *
 *  3. Add the observer by:
 *       commandManager.addObserver(observeobject, obs_documentCreated);
 *  4. In the appropriate location in editorSession, editor, or commands code,
 *     trigger the notification of this observer by something like:
 *
 *  RefPtr<nsCommandManager> commandManager = mDocShell->GetCommandManager();
 *  commandManager->CommandStatusChanged(obs_documentCreated);
 *
 *  5. Use GetCommandStateParams() to obtain state information
 *     e.g., any creation state codes when creating an editor are
 *     supplied for "obs_documentCreated" command in the
 *     "state_data" param's value
 *****************************************************************************/

StaticRefPtr<DocumentStateCommand> DocumentStateCommand::sInstance;

bool DocumentStateCommand::IsCommandEnabled(Command /*aCommand*/,
                                            EditorBase* /*aEditorBase*/) const {
  // Always return false to discourage callers from using DoCommand()
  return false;
}

nsresult DocumentStateCommand::DoCommand(Command /*aCommand*/,
                                         EditorBase& /*aEditorBase*/,
                                         nsIPrincipal* /*aPrincipal*/) const {
  return NS_ERROR_NOT_IMPLEMENTED;
}

nsresult DocumentStateCommand::GetCommandStateParams(
    Command aCommand, nsCommandParams& aParams, EditorBase* aEditorBase,
    nsIEditingSession* aEditingSession) const {
  switch (aCommand) {
    case Command::EditorObserverDocumentCreated: {
      uint32_t editorStatus = nsIEditingSession::eEditorErrorUnknown;
      if (aEditingSession) {
        // Current context is initially set to nsIEditingSession until editor is
        // successfully created and source doc is loaded.  Embedder gets error
        // status if this fails.  If called before startup is finished,
        // status will be eEditorCreationInProgress.
        nsresult rv = aEditingSession->GetEditorStatus(&editorStatus);
        if (NS_FAILED(rv)) {
          NS_WARNING("nsIEditingSession::GetEditorStatus() failed");
          return rv;
        }
      } else if (aEditorBase) {
        // If current context is an editor, then everything started up OK!
        editorStatus = nsIEditingSession::eEditorOK;
      }

      // Note that if refCon is not-null, but is neither
      // an nsIEditingSession or nsIEditor, we return "eEditorErrorUnknown"
      DebugOnly<nsresult> rvIgnored = aParams.SetInt(STATE_DATA, editorStatus);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rvIgnored),
                           "Failed to set editor status");
      return NS_OK;
    }
    case Command::EditorObserverDocumentLocationChanged: {
      if (!aEditorBase) {
        return NS_OK;
      }
      Document* document = aEditorBase->GetDocument();
      if (NS_WARN_IF(!document)) {
        return NS_ERROR_FAILURE;
      }
      nsIURI* uri = document->GetDocumentURI();
      if (NS_WARN_IF(!uri)) {
        return NS_ERROR_FAILURE;
      }
      nsresult rv = aParams.SetISupports(STATE_DATA, uri);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                           "nsCOmmandParms::SetISupports(STATE_DATA) failed");
      return rv;
    }
    default:
      return NS_ERROR_NOT_IMPLEMENTED;
  }
}

}  // namespace mozilla
