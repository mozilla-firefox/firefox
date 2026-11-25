/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsGlobalWindowCommands.h"

#include "ContentEventHandler.h"
#include "ErrorList.h"
#include "mozilla/Attributes.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/ControllerCommand.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_accessibility.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TextEvents.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/EditContext.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/intl/WordBreaker.h"
#include "mozilla/layers/KeyboardMap.h"
#include "nsCRT.h"
#include "nsCommandParams.h"
#include "nsContentUtils.h"
#include "nsControllerCommandTable.h"
#include "nsCopySupport.h"
#include "nsFocusManager.h"
#include "nsIClipboard.h"
#include "nsIDocShell.h"
#include "nsIDocumentViewer.h"
#include "nsIDocumentViewerEdit.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsISelectionController.h"
#include "nsIWebNavigation.h"
#include "nsPIDOMWindow.h"
#include "nsString.h"

using namespace mozilla;
using namespace mozilla::layers;

constexpr nsLiteralCString kSelectMoveScrollCommands[] = {
    "cmd_beginLine"_ns,    "cmd_beginParagraph"_ns, "cmd_charNext"_ns,
    "cmd_charPrevious"_ns, "cmd_endLine"_ns,        "cmd_endParagraph"_ns,
    "cmd_lineNext"_ns,     "cmd_linePrevious"_ns,   "cmd_moveBottom"_ns,
    "cmd_movePageDown"_ns, "cmd_movePageUp"_ns,     "cmd_moveTop"_ns,
    "cmd_scrollBottom"_ns, "cmd_scrollLeft"_ns,     "cmd_scrollLineDown"_ns,
    "cmd_scrollLineUp"_ns, "cmd_scrollPageDown"_ns, "cmd_scrollPageUp"_ns,
    "cmd_scrollRight"_ns,  "cmd_scrollTop"_ns,      "cmd_wordNext"_ns,
    "cmd_wordPrevious"_ns,
};

// These are so the browser can use editor navigation key bindings
// helps with accessibility (boolean pref accessibility.browsewithcaret)
constexpr nsLiteralCString kSelectCommands[] = {
    "cmd_selectBeginLine"_ns,    "cmd_selectBeginParagraph"_ns,
    "cmd_selectBottom"_ns,       "cmd_selectCharNext"_ns,
    "cmd_selectCharPrevious"_ns, "cmd_selectEndLine"_ns,
    "cmd_selectEndParagraph"_ns, "cmd_selectLineNext"_ns,
    "cmd_selectLinePrevious"_ns, "cmd_selectPageDown"_ns,
    "cmd_selectPageUp"_ns,       "cmd_selectTop"_ns,
    "cmd_selectWordNext"_ns,     "cmd_selectWordPrevious"_ns,
};

// Physical-direction movement and selection commands
constexpr nsLiteralCString kPhysicalSelectMoveScrollCommands[] = {
    "cmd_moveDown"_ns,   "cmd_moveDown2"_ns,  "cmd_moveLeft"_ns,
    "cmd_moveLeft2"_ns,  "cmd_moveLeft3"_ns,  "cmd_moveRight"_ns,
    "cmd_moveRight2"_ns, "cmd_moveRight3"_ns, "cmd_moveUp"_ns,
    "cmd_moveUp2"_ns,
};

constexpr nsLiteralCString kPhysicalSelectCommands[] = {
    "cmd_selectDown"_ns,   "cmd_selectDown2"_ns,  "cmd_selectLeft"_ns,
    "cmd_selectLeft2"_ns,  "cmd_selectLeft3"_ns,  "cmd_selectRight"_ns,
    "cmd_selectRight2"_ns, "cmd_selectRight3"_ns, "cmd_selectUp"_ns,
    "cmd_selectUp2"_ns,
};

// a base class for selection-related commands, for code sharing
class nsSelectionCommandsBase : public ControllerCommand {
 public:
  bool IsCommandEnabled(const nsACString&, nsISupports*) override {
    dom::Element* element = nsFocusManager::GetFocusedElementStatic();
    if (!element) {
      return true;
    }
    // Disable all selection commands for canvas-based EditContext,
    // since the web app handles the selection for it.
    dom::EditContext* editContext = element->OwnerDoc()->GetActiveEditContext();
    return !editContext || !editContext->IsCanvas();
  }
  void GetCommandStateParams(const nsACString&, nsICommandParams*,
                             nsISupports*) override {}

 protected:
  virtual ~nsSelectionCommandsBase() = default;

  static nsresult GetPresShellFromWindow(nsPIDOMWindowOuter* aWindow,
                                         PresShell** aPresShell);
  static nsresult GetSelectionControllerFromWindow(
      nsPIDOMWindowOuter* aWindow, nsISelectionController** aSelCon);

  // no member variables, please, we're stateless!
};

// this class implements commands whose behavior depends on the 'browse with
// caret' setting
class nsSelectMoveScrollCommand : public nsSelectionCommandsBase {
 public:
  MOZ_CAN_RUN_SCRIPT nsresult DoCommand(const nsACString& aCommandName,
                                        nsICommandParams*,
                                        nsISupports* aCommandContext) override;
};

// this class implements physical-movement versions of the above
class nsPhysicalSelectMoveScrollCommand : public nsSelectionCommandsBase {
 public:
  MOZ_CAN_RUN_SCRIPT nsresult DoCommand(const nsACString& aCommandName,
                                        nsICommandParams*,
                                        nsISupports* aCommandContext) override;
};

// this class implements other selection commands
class nsSelectCommand : public nsSelectionCommandsBase {
 public:
  MOZ_CAN_RUN_SCRIPT nsresult DoCommand(const nsACString& aCommandName,
                                        nsICommandParams*,
                                        nsISupports* aCommandContext) override;
};

// this class implements physical-movement versions of selection commands
class nsPhysicalSelectCommand : public nsSelectionCommandsBase {
 public:
  MOZ_CAN_RUN_SCRIPT nsresult DoCommand(const nsACString& aCommandName,
                                        nsICommandParams*,
                                        nsISupports* aCommandContext) override;

  // no member variables, please, we're stateless!
};

nsresult nsSelectionCommandsBase::GetPresShellFromWindow(
    nsPIDOMWindowOuter* aWindow, PresShell** aPresShell) {
  *aPresShell = nullptr;
  NS_ENSURE_TRUE(aWindow, NS_ERROR_FAILURE);

  nsIDocShell* docShell = aWindow->GetDocShell();
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  NS_IF_ADDREF(*aPresShell = docShell->GetPresShell());
  return NS_OK;
}

nsresult nsSelectionCommandsBase::GetSelectionControllerFromWindow(
    nsPIDOMWindowOuter* aWindow, nsISelectionController** aSelCon) {
  RefPtr<PresShell> presShell;
  GetPresShellFromWindow(aWindow, getter_AddRefs(presShell));
  if (!presShell) {
    *aSelCon = nullptr;
    return NS_ERROR_FAILURE;
  }
  *aSelCon = presShell.forget().take();
  return NS_OK;
}

// Helpers for nsSelectMoveScrollCommand and nsPhysicalSelectMoveScrollCommand
static void AdjustFocusAfterCaretMove(nsPIDOMWindowOuter* aWindow) {
  // adjust the focus to the new caret position
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    RefPtr<dom::Element> result;
    fm->MoveFocus(aWindow, nullptr, nsIFocusManager::MOVEFOCUS_CARET,
                  nsIFocusManager::FLAG_NOSCROLL, getter_AddRefs(result));
  }
}

static bool IsCaretOnInWindow(nsPIDOMWindowOuter* aWindow,
                              nsISelectionController* aSelCont) {
  // We allow the caret to be moved with arrow keys on any window for which
  // the caret is enabled. In particular, this includes caret-browsing mode
  // in non-chrome documents.
  bool caretOn = false;
  aSelCont->GetCaretEnabled(&caretOn);
  if (!caretOn) {
    caretOn = StaticPrefs::accessibility_browsewithcaret();
    if (caretOn) {
      nsCOMPtr<nsIDocShell> docShell = aWindow->GetDocShell();
      if (docShell && docShell->ItemType() == nsIDocShellTreeItem::typeChrome) {
        caretOn = false;
      }
    }
  }
  return caretOn;
}

static constexpr struct BrowseCommand {
  Command reverse, forward;
  KeyboardScrollAction::KeyboardScrollActionType scrollAction;
  nsresult (NS_STDCALL nsISelectionController::*scroll)(bool);
  nsresult (NS_STDCALL nsISelectionController::*move)(bool, bool);
} browseCommands[] = {
    {Command::ScrollTop, Command::ScrollBottom,
     KeyboardScrollAction::eScrollComplete,
     &nsISelectionController::CompleteScroll},
    {Command::ScrollPageUp, Command::ScrollPageDown,
     KeyboardScrollAction::eScrollPage, &nsISelectionController::ScrollPage},
    {Command::ScrollLineUp, Command::ScrollLineDown,
     KeyboardScrollAction::eScrollLine, &nsISelectionController::ScrollLine},
    {Command::ScrollLeft, Command::ScrollRight,
     KeyboardScrollAction::eScrollCharacter,
     &nsISelectionController::ScrollCharacter},
    {Command::MoveTop, Command::MoveBottom,
     KeyboardScrollAction::eScrollComplete,
     &nsISelectionController::CompleteScroll,
     &nsISelectionController::CompleteMove},
    {Command::MovePageUp, Command::MovePageDown,
     KeyboardScrollAction::eScrollPage, &nsISelectionController::ScrollPage,
     &nsISelectionController::PageMove},
    {Command::LinePrevious, Command::LineNext,
     KeyboardScrollAction::eScrollLine, &nsISelectionController::ScrollLine,
     &nsISelectionController::LineMove},
    {Command::WordPrevious, Command::WordNext,
     KeyboardScrollAction::eScrollCharacter,
     &nsISelectionController::ScrollCharacter,
     &nsISelectionController::WordMove},
    {Command::CharPrevious, Command::CharNext,
     KeyboardScrollAction::eScrollCharacter,
     &nsISelectionController::ScrollCharacter,
     &nsISelectionController::CharacterMove},
    {Command::BeginLine, Command::EndLine,
     KeyboardScrollAction::eScrollComplete,
     &nsISelectionController::CompleteScroll,
     &nsISelectionController::IntraLineMove},
    {Command::BeginParagraph, Command::EndParagraph,
     KeyboardScrollAction::eScrollComplete,
     &nsISelectionController::CompleteScroll,
     &nsISelectionController::ParagraphMove}};

nsresult nsSelectMoveScrollCommand::DoCommand(const nsACString& aCommandName,
                                              nsICommandParams*,
                                              nsISupports* aCommandContext) {
  nsCOMPtr<nsPIDOMWindowOuter> piWindow(do_QueryInterface(aCommandContext));
  nsCOMPtr<nsISelectionController> selCont;
  GetSelectionControllerFromWindow(piWindow, getter_AddRefs(selCont));
  NS_ENSURE_TRUE(selCont, NS_ERROR_NOT_INITIALIZED);

  const bool caretOn = IsCaretOnInWindow(piWindow, selCont);
  const Command command = GetInternalCommand(aCommandName);
  for (const BrowseCommand& browseCommand : browseCommands) {
    const bool forward = command == browseCommand.forward;
    if (!forward && command != browseCommand.reverse) {
      continue;
    }
    RefPtr<HTMLEditor> htmlEditor =
        HTMLEditor::GetFrom(nsContentUtils::GetActiveEditor(piWindow));
    if (htmlEditor) {
      htmlEditor->PreHandleSelectionChangeCommand(command);
    }
    nsresult rv = NS_OK;
    if (caretOn && browseCommand.move &&
        NS_SUCCEEDED((selCont->*(browseCommand.move))(forward, false))) {
      AdjustFocusAfterCaretMove(piWindow);
    } else {
      rv = (selCont->*(browseCommand.scroll))(forward);
    }
    if (htmlEditor) {
      htmlEditor->PostHandleSelectionChangeCommand(command);
    }
    return rv;
  }

  MOZ_ASSERT(false, "Forgot to handle new command?");
  return NS_ERROR_NOT_IMPLEMENTED;
}

// XXX It's not clear to me yet how we should handle the "scroll" option
// for these commands; for now, I'm mapping them back to ScrollCharacter,
// ScrollLine, etc., as if for horizontal-mode content, but this may need
// to be reconsidered once we have more experience with vertical content.
static const struct PhysicalBrowseCommand {
  Command command;
  int16_t direction, amount;
  KeyboardScrollAction::KeyboardScrollActionType scrollAction;
  nsresult (NS_STDCALL nsISelectionController::*scroll)(bool);
} physicalBrowseCommands[] = {
    {Command::MoveLeft, nsISelectionController::MOVE_LEFT, 0,
     KeyboardScrollAction::eScrollCharacter,
     &nsISelectionController::ScrollCharacter},
    {Command::MoveRight, nsISelectionController::MOVE_RIGHT, 0,
     KeyboardScrollAction::eScrollCharacter,
     &nsISelectionController::ScrollCharacter},
    {Command::MoveUp, nsISelectionController::MOVE_UP, 0,
     KeyboardScrollAction::eScrollLine, &nsISelectionController::ScrollLine},
    {Command::MoveDown, nsISelectionController::MOVE_DOWN, 0,
     KeyboardScrollAction::eScrollLine, &nsISelectionController::ScrollLine},
    {Command::MoveLeft2, nsISelectionController::MOVE_LEFT, 1,
     KeyboardScrollAction::eScrollCharacter,
     &nsISelectionController::ScrollCharacter},
    {Command::MoveRight2, nsISelectionController::MOVE_RIGHT, 1,
     KeyboardScrollAction::eScrollCharacter,
     &nsISelectionController::ScrollCharacter},
    {Command::MoveUp2, nsISelectionController::MOVE_UP, 1,
     KeyboardScrollAction::eScrollComplete,
     &nsISelectionController::CompleteScroll},
    {Command::MoveDown2, nsISelectionController::MOVE_DOWN, 1,
     KeyboardScrollAction::eScrollComplete,
     &nsISelectionController::CompleteScroll},
    {Command::MoveLeft3, nsISelectionController::MOVE_LEFT, 2,
     KeyboardScrollAction::eScrollComplete,
     &nsISelectionController::CompleteScroll},
    {Command::MoveRight3, nsISelectionController::MOVE_RIGHT, 2,
     KeyboardScrollAction::eScrollComplete,
     &nsISelectionController::CompleteScroll},
};

nsresult nsPhysicalSelectMoveScrollCommand::DoCommand(
    const nsACString& aCommandName, nsICommandParams*,
    nsISupports* aCommandContext) {
  nsCOMPtr<nsPIDOMWindowOuter> piWindow(do_QueryInterface(aCommandContext));
  nsCOMPtr<nsISelectionController> selCont;
  GetSelectionControllerFromWindow(piWindow, getter_AddRefs(selCont));
  NS_ENSURE_TRUE(selCont, NS_ERROR_NOT_INITIALIZED);

  const bool caretOn = IsCaretOnInWindow(piWindow, selCont);
  Command command = GetInternalCommand(aCommandName);
  for (const PhysicalBrowseCommand& browseCommand : physicalBrowseCommands) {
    if (command != browseCommand.command) {
      continue;
    }
    RefPtr<HTMLEditor> htmlEditor =
        HTMLEditor::GetFrom(nsContentUtils::GetActiveEditor(piWindow));
    if (htmlEditor) {
      htmlEditor->PreHandleSelectionChangeCommand(command);
    }
    nsresult rv = NS_OK;
    if (caretOn && NS_SUCCEEDED(selCont->PhysicalMove(
                       browseCommand.direction, browseCommand.amount, false))) {
      AdjustFocusAfterCaretMove(piWindow);
    } else {
      const bool forward =
          (browseCommand.direction == nsISelectionController::MOVE_RIGHT ||
           browseCommand.direction == nsISelectionController::MOVE_DOWN);
      rv = (selCont->*(browseCommand.scroll))(forward);
    }
    if (htmlEditor) {
      htmlEditor->PostHandleSelectionChangeCommand(command);
    }
    return rv;
  }

  MOZ_ASSERT(false, "Forgot to handle new command?");
  return NS_ERROR_NOT_IMPLEMENTED;
}

static const struct SelectCommand {
  Command reverse, forward;
  nsresult (NS_STDCALL nsISelectionController::*select)(bool, bool);
} selectCommands[] = {
    {Command::SelectCharPrevious, Command::SelectCharNext,
     &nsISelectionController::CharacterMove},
    {Command::SelectWordPrevious, Command::SelectWordNext,
     &nsISelectionController::WordMove},
    {Command::SelectBeginLine, Command::SelectEndLine,
     &nsISelectionController::IntraLineMove},
    {Command::SelectBeginParagraph, Command::SelectEndParagraph,
     &nsISelectionController::ParagraphMove},
    {Command::SelectLinePrevious, Command::SelectLineNext,
     &nsISelectionController::LineMove},
    {Command::SelectPageUp, Command::SelectPageDown,
     &nsISelectionController::PageMove},
    {Command::SelectTop, Command::SelectBottom,
     &nsISelectionController::CompleteMove}};

nsresult nsSelectCommand::DoCommand(const nsACString& aCommandName,
                                    nsICommandParams*,
                                    nsISupports* aCommandContext) {
  nsCOMPtr<nsPIDOMWindowOuter> piWindow(do_QueryInterface(aCommandContext));
  nsCOMPtr<nsISelectionController> selCont;
  GetSelectionControllerFromWindow(piWindow, getter_AddRefs(selCont));
  NS_ENSURE_TRUE(selCont, NS_ERROR_NOT_INITIALIZED);

  // These commands are so the browser can use caret navigation key bindings -
  // Helps with accessibility - aaronl@netscape.com
  const Command command = GetInternalCommand(aCommandName);
  for (const SelectCommand& selectCommand : selectCommands) {
    const bool forward = command == selectCommand.forward;
    if (!forward && command != selectCommand.reverse) {
      continue;
    }
    RefPtr<HTMLEditor> htmlEditor =
        HTMLEditor::GetFrom(nsContentUtils::GetActiveEditor(piWindow));
    if (htmlEditor) {
      htmlEditor->PreHandleSelectionChangeCommand(command);
    }
    nsresult rv = (selCont->*(selectCommand.select))(forward, true);
    if (htmlEditor) {
      htmlEditor->PostHandleSelectionChangeCommand(command);
    }
    return rv;
  }

  MOZ_ASSERT(false, "Forgot to handle new command?");
  return NS_ERROR_NOT_IMPLEMENTED;
}

static const struct PhysicalSelectCommand {
  Command command;
  int16_t direction, amount;
} physicalSelectCommands[] = {
    {Command::SelectLeft, nsISelectionController::MOVE_LEFT, 0},
    {Command::SelectRight, nsISelectionController::MOVE_RIGHT, 0},
    {Command::SelectUp, nsISelectionController::MOVE_UP, 0},
    {Command::SelectDown, nsISelectionController::MOVE_DOWN, 0},
    {Command::SelectLeft2, nsISelectionController::MOVE_LEFT, 1},
    {Command::SelectRight2, nsISelectionController::MOVE_RIGHT, 1},
    {Command::SelectUp2, nsISelectionController::MOVE_UP, 1},
    {Command::SelectDown2, nsISelectionController::MOVE_DOWN, 1},
    {Command::SelectLeft3, nsISelectionController::MOVE_LEFT, 2},
    {Command::SelectRight3, nsISelectionController::MOVE_RIGHT, 2}};

nsresult nsPhysicalSelectCommand::DoCommand(const nsACString& aCommandName,
                                            nsICommandParams* aParams,
                                            nsISupports* aCommandContext) {
  nsCOMPtr<nsPIDOMWindowOuter> piWindow(do_QueryInterface(aCommandContext));
  nsCOMPtr<nsISelectionController> selCont;
  GetSelectionControllerFromWindow(piWindow, getter_AddRefs(selCont));
  NS_ENSURE_TRUE(selCont, NS_ERROR_NOT_INITIALIZED);

  const Command command = GetInternalCommand(aCommandName);
  for (const PhysicalSelectCommand& selectCommand : physicalSelectCommands) {
    if (command != selectCommand.command) {
      continue;
    }
    RefPtr<HTMLEditor> htmlEditor =
        HTMLEditor::GetFrom(nsContentUtils::GetActiveEditor(piWindow));
    if (htmlEditor) {
      htmlEditor->PreHandleSelectionChangeCommand(command);
    }
    nsresult rv = selCont->PhysicalMove(selectCommand.direction,
                                        selectCommand.amount, true);
    if (htmlEditor) {
      htmlEditor->PostHandleSelectionChangeCommand(command);
    }
    return rv;
  }

  MOZ_ASSERT(false, "Forgot to handle new command?");
  return NS_ERROR_NOT_IMPLEMENTED;
}

class nsClipboardCommand final : public ControllerCommand {
 public:
  DECL_CONTROLLER_COMMAND_NO_PARAMS
};

bool nsClipboardCommand::IsCommandEnabled(const nsACString& aCommandName,
                                          nsISupports* aContext) {
  nsCOMPtr<nsPIDOMWindowOuter> window = do_QueryInterface(aContext);
  if (!window) {
    return false;
  }
  RefPtr<dom::Document> doc = window->GetExtantDoc();
  if (!doc) {
    return false;
  }
  if (doc->AreClipboardCommandsUnconditionallyEnabled()) {
    // In HTML and XHTML documents, we always want the cut, copy and paste
    // commands to be enabled, but if the document is chrome, let it control it.
    return true;
  }
  if (aCommandName.EqualsLiteral("cmd_copy")) {
    // Cut isn't enabled in xul documents which use nsClipboardCommand
    return nsCopySupport::CanCopy(doc);
  }
  return false;
}

nsresult nsClipboardCommand::DoCommand(const nsACString& aCommandName,
                                       nsICommandParams*,
                                       nsISupports* aContext) {
  nsCOMPtr<nsPIDOMWindowOuter> window = do_QueryInterface(aContext);
  NS_ENSURE_TRUE(window, NS_ERROR_FAILURE);

  nsIDocShell* docShell = window->GetDocShell();
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  RefPtr<PresShell> presShell = docShell->GetPresShell();
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);

  const EventMessage eventMessage = [&] {
    if (aCommandName.EqualsLiteral("cmd_cut")) {
      return eCut;
    }
    if (aCommandName.EqualsLiteral("cmd_paste")) {
      return ePaste;
    }
    MOZ_ASSERT(aCommandName.EqualsLiteral("cmd_copy"));
    return eCopy;
  }();

  RefPtr<dom::DataTransfer> dataTransfer;
  if (ePaste == eventMessage) {
    nsCOMPtr<nsIPrincipal> subjectPrincipal =
        nsContentUtils::SubjectPrincipalOrSystemIfNativeCaller();
    MOZ_ASSERT(subjectPrincipal);

    // If we don't need to get user confirmation for clipboard access, we could
    // just let nsCopySupport::FireClipboardEvent() to create DataTransfer
    // instance synchronously for paste event. Otherwise, we need to spin the
    // event loop to wait for the clipboard paste contextmenu to be shown and
    // get user confirmation which are all handled in parent process before
    // sending the paste event.
    if (!nsContentUtils::PrincipalHasPermission(*subjectPrincipal,
                                                nsGkAtoms::clipboardRead) &&
        !dom::UserActivation::IsHandlingKeyboardInputWithPasteActions()) {
      MOZ_DIAGNOSTIC_ASSERT(StaticPrefs::dom_execCommand_paste_enabled(),
                            "How did we get here?");
      // This will spin the event loop.
      dataTransfer = dom::DataTransfer::WaitForClipboardDataSnapshotAndCreate(
          window, subjectPrincipal);
      if (!dataTransfer) {
        return NS_SUCCESS_DOM_NO_OPERATION;
      }
    }
  }

  bool actionTaken = false;
  nsCopySupport::FireClipboardEvent(
      eventMessage, Some(nsIClipboard::kGlobalClipboard), presShell, nullptr,
      dataTransfer, &actionTaken);

  return actionTaken ? NS_OK : NS_SUCCESS_DOM_NO_OPERATION;
}

class nsSelectionCommand : public ControllerCommand {
 public:
  DECL_CONTROLLER_COMMAND_NO_PARAMS

 protected:
  virtual ~nsSelectionCommand() = default;

  virtual bool IsClipboardCommandEnabled(const nsACString& aCommandName,
                                         nsIDocumentViewerEdit* aEdit) = 0;
  virtual nsresult DoClipboardCommand(const nsACString& aCommandName,
                                      nsIDocumentViewerEdit* aEdit,
                                      nsICommandParams* aParams) = 0;

  static already_AddRefed<nsIDocumentViewerEdit>
  GetDocumentViewerEditFromContext(nsISupports* aContext);

  // no member variables, please, we're stateless!
};

bool nsSelectionCommand::IsCommandEnabled(const nsACString& aCommandName,
                                          nsISupports* aCommandContext) {
  nsCOMPtr<nsIDocumentViewerEdit> documentEdit =
      GetDocumentViewerEditFromContext(aCommandContext);
  return IsClipboardCommandEnabled(aCommandName, documentEdit);
}

nsresult nsSelectionCommand::DoCommand(const nsACString& aCommandName,
                                       nsICommandParams* aParams,
                                       nsISupports* aCommandContext) {
  nsCOMPtr<nsIDocumentViewerEdit> documentEdit =
      GetDocumentViewerEditFromContext(aCommandContext);
  NS_ENSURE_TRUE(documentEdit, NS_ERROR_NOT_INITIALIZED);
  return DoClipboardCommand(aCommandName, documentEdit, aParams);
}

already_AddRefed<nsIDocumentViewerEdit>
nsSelectionCommand::GetDocumentViewerEditFromContext(nsISupports* aContext) {
  nsCOMPtr<nsPIDOMWindowOuter> window = do_QueryInterface(aContext);
  if (!window) {
    return nullptr;
  }

  nsIDocShell* docShell = window->GetDocShell();
  NS_ENSURE_TRUE(docShell, nullptr);

  nsCOMPtr<nsIDocumentViewer> viewer;
  docShell->GetDocViewer(getter_AddRefs(viewer));
  nsCOMPtr<nsIDocumentViewerEdit> edit(do_QueryInterface(viewer));
  return edit.forget();
}

#define NS_DECL_CLIPBOARD_COMMAND(_cmd)                                    \
  class _cmd : public nsSelectionCommand {                                 \
   protected:                                                              \
    bool IsClipboardCommandEnabled(const nsACString& aCommandName,         \
                                   nsIDocumentViewerEdit* aEdit) override; \
    nsresult DoClipboardCommand(const nsACString& aCommandName,            \
                                nsIDocumentViewerEdit* aEdit,              \
                                nsICommandParams* aParams) override;       \
    /* no member variables, please, we're stateless! */                    \
  };

NS_DECL_CLIPBOARD_COMMAND(nsClipboardCopyLinkCommand)
NS_DECL_CLIPBOARD_COMMAND(nsClipboardImageCommands)
NS_DECL_CLIPBOARD_COMMAND(nsClipboardSelectAllNoneCommands)

bool nsClipboardCopyLinkCommand::IsClipboardCommandEnabled(
    const nsACString& aCommandName, nsIDocumentViewerEdit* aEdit) {
  return aEdit && aEdit->GetInLink();
}

nsresult nsClipboardCopyLinkCommand::DoClipboardCommand(
    const nsACString& aCommandName, nsIDocumentViewerEdit* aEdit,
    nsICommandParams* aParams) {
  return aEdit->CopyLinkLocation();
}

bool nsClipboardImageCommands::IsClipboardCommandEnabled(
    const nsACString& aCommandName, nsIDocumentViewerEdit* aEdit) {
  return aEdit && aEdit->GetInImage();
}

nsresult nsClipboardImageCommands::DoClipboardCommand(
    const nsACString& aCommandName, nsIDocumentViewerEdit* aEdit,
    nsICommandParams* aParams) {
  if (aCommandName.EqualsLiteral("cmd_copyImageLocation")) {
    return aEdit->CopyImage(nsIDocumentViewerEdit::COPY_IMAGE_TEXT);
  }
  if (aCommandName.EqualsLiteral("cmd_copyImageContents")) {
    return aEdit->CopyImage(nsIDocumentViewerEdit::COPY_IMAGE_DATA);
  }
  int32_t copyFlags = nsIDocumentViewerEdit::COPY_IMAGE_DATA |
                      nsIDocumentViewerEdit::COPY_IMAGE_HTML;
  if (aParams) {
    copyFlags = aParams->AsCommandParams()->GetInt("imageCopy");
  }
  return aEdit->CopyImage(copyFlags);
}

bool nsClipboardSelectAllNoneCommands::IsClipboardCommandEnabled(
    const nsACString& aCommandName, nsIDocumentViewerEdit* aEdit) {
  return true;
}

nsresult nsClipboardSelectAllNoneCommands::DoClipboardCommand(
    const nsACString& aCommandName, nsIDocumentViewerEdit* aEdit,
    nsICommandParams* aParams) {
  if (aCommandName.EqualsLiteral("cmd_selectAll")) {
    return aEdit->SelectAll();
  }
  return aEdit->ClearSelection();
}

class nsLookUpDictionaryCommand final : public ControllerCommand {
 public:
  DECL_CONTROLLER_COMMAND_NO_PARAMS
};

bool nsLookUpDictionaryCommand::IsCommandEnabled(const nsACString& aCommandName,
                                                 nsISupports* aCommandContext) {
  return true;
}

nsresult nsLookUpDictionaryCommand::DoCommand(const nsACString& aCommandName,
                                              nsICommandParams* aParams,
                                              nsISupports* aCommandContext) {
  if (NS_WARN_IF(!nsContentUtils::IsSafeToRunScript())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (!aParams) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  nsCommandParams* params = aParams->AsCommandParams();

  ErrorResult error;
  int32_t x = params->GetInt("x", error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }
  int32_t y = params->GetInt("y", error);
  if (NS_WARN_IF(error.Failed())) {
    return error.StealNSResult();
  }

  LayoutDeviceIntPoint point(x, y);

  nsCOMPtr<nsPIDOMWindowOuter> window = do_QueryInterface(aCommandContext);
  if (NS_WARN_IF(!window)) {
    return NS_ERROR_FAILURE;
  }

  nsIDocShell* docShell = window->GetDocShell();
  if (NS_WARN_IF(!docShell)) {
    return NS_ERROR_FAILURE;
  }

  PresShell* presShell = docShell->GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return NS_ERROR_FAILURE;
  }

  nsPresContext* presContext = presShell->GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIWidget> widget = presContext->GetRootWidget();
  if (NS_WARN_IF(!widget)) {
    return NS_ERROR_FAILURE;
  }

  WidgetQueryContentEvent queryCharAtPointEvent(true, eQueryCharacterAtPoint,
                                                widget);
  queryCharAtPointEvent.mRefPoint.x = x;
  queryCharAtPointEvent.mRefPoint.y = y;
  ContentEventHandler handler(presContext);
  handler.OnQueryCharacterAtPoint(&queryCharAtPointEvent);

  if (NS_WARN_IF(queryCharAtPointEvent.Failed()) ||
      queryCharAtPointEvent.DidNotFindChar()) {
    return NS_ERROR_FAILURE;
  }

  WidgetQueryContentEvent querySelectedTextEvent(true, eQuerySelectedText,
                                                 widget);
  handler.OnQuerySelectedText(&querySelectedTextEvent);
  if (NS_WARN_IF(querySelectedTextEvent.DidNotFindSelection())) {
    return NS_ERROR_FAILURE;
  }

  uint32_t offset = queryCharAtPointEvent.mReply->StartOffset();
  uint32_t begin, length;

  // macOS prioritizes user selected text if the current point falls within the
  // selection range. So we check the selection first.
  if (querySelectedTextEvent.FoundSelection() &&
      querySelectedTextEvent.mReply->IsOffsetInRange(offset)) {
    begin = querySelectedTextEvent.mReply->StartOffset();
    length = querySelectedTextEvent.mReply->DataLength();
  } else {
    WidgetQueryContentEvent queryTextContentEvent(true, eQueryTextContent,
                                                  widget);
    // OSX 10.7 queries 50 characters before/after current point.  So we fetch
    // same length.
    if (offset > 50) {
      offset -= 50;
    } else {
      offset = 0;
    }
    queryTextContentEvent.InitForQueryTextContent(offset, 100);
    handler.OnQueryTextContent(&queryTextContentEvent);
    if (NS_WARN_IF(queryTextContentEvent.Failed()) ||
        NS_WARN_IF(queryTextContentEvent.mReply->IsDataEmpty())) {
      return NS_ERROR_FAILURE;
    }

    intl::WordRange range = intl::WordBreaker::FindWord(
        queryTextContentEvent.mReply->DataRef(),
        queryCharAtPointEvent.mReply->StartOffset() - offset);
    if (range.mEnd == range.mBegin) {
      return NS_ERROR_FAILURE;
    }
    begin = range.mBegin + offset;
    length = range.mEnd - range.mBegin;
  }

  WidgetQueryContentEvent queryLookUpContentEvent(true, eQueryTextContent,
                                                  widget);
  queryLookUpContentEvent.InitForQueryTextContent(begin, length);
  queryLookUpContentEvent.RequestFontRanges();
  handler.OnQueryTextContent(&queryLookUpContentEvent);
  if (NS_WARN_IF(queryLookUpContentEvent.Failed()) ||
      NS_WARN_IF(queryLookUpContentEvent.mReply->IsDataEmpty())) {
    return NS_ERROR_FAILURE;
  }

  WidgetQueryContentEvent queryTextRectEvent(true, eQueryTextRect, widget);
  queryTextRectEvent.InitForQueryTextRect(begin, length);
  handler.OnQueryTextRect(&queryTextRectEvent);
  if (NS_WARN_IF(queryTextRectEvent.Failed())) {
    return NS_ERROR_FAILURE;
  }

  widget->LookUpDictionary(queryLookUpContentEvent.mReply->DataRef(),
                           queryLookUpContentEvent.mReply->mFontRanges,
                           queryTextRectEvent.mReply->mWritingMode.IsVertical(),
                           queryTextRectEvent.mReply->mRect.TopLeft());

  return NS_OK;
}

/*---------------------------------------------------------------------------

  RegisterWindowCommands

----------------------------------------------------------------------------*/

// static
void nsWindowCommandRegistration::RegisterWindowCommands(
    nsControllerCommandTable* aCommandTable) {
  {
    RefPtr command = new nsSelectMoveScrollCommand();
    for (const auto& name : kSelectMoveScrollCommands) {
      aCommandTable->RegisterCommand(name, command);
    }
  }

  {
    RefPtr command = new nsPhysicalSelectMoveScrollCommand();
    for (const auto& name : kPhysicalSelectMoveScrollCommands) {
      aCommandTable->RegisterCommand(name, command);
    }
  }

  {
    RefPtr command = new nsSelectCommand();
    for (const auto& name : kSelectCommands) {
      aCommandTable->RegisterCommand(name, command);
    }
  }

  {
    RefPtr command = new nsPhysicalSelectCommand();
    for (const auto& name : kPhysicalSelectCommands) {
      aCommandTable->RegisterCommand(name, command);
    }
  }

  {
    RefPtr command = new nsClipboardCommand();
    for (const auto& name : {"cmd_cut"_ns, "cmd_copy"_ns, "cmd_paste"_ns}) {
      aCommandTable->RegisterCommand(name, command);
    }
  }

  aCommandTable->RegisterCommand("cmd_copyLink"_ns,
                                 new nsClipboardCopyLinkCommand());

  {
    RefPtr command = new nsClipboardImageCommands();
    for (const auto& name : {
             "cmd_copyImageLocation"_ns,
             "cmd_copyImageContents"_ns,
             "cmd_copyImage"_ns,
         }) {
      aCommandTable->RegisterCommand(name, command);
    }
  }

  {
    RefPtr command = new nsClipboardSelectAllNoneCommands();
    for (const auto& name : {"cmd_selectAll"_ns, "cmd_selectNone"_ns}) {
      aCommandTable->RegisterCommand(name, command);
    }
  }

  aCommandTable->RegisterCommand("cmd_lookUpDictionary"_ns,
                                 new nsLookUpDictionaryCommand());
}

/* static */
bool nsGlobalWindowCommands::FindScrollCommand(
    const nsACString& aCommandName, KeyboardScrollAction* aOutAction) {
  // Search for a keyboard scroll action to do for this command in
  // browseCommands and physicalBrowseCommands. Each command exists in only one
  // of them, so the order we examine browseCommands and physicalBrowseCommands
  // doesn't matter.

  const Command command = GetInternalCommand(aCommandName);
  if (command == Command::DoNothing) {
    return false;
  }
  for (const BrowseCommand& browseCommand : browseCommands) {
    const bool forward = command == browseCommand.forward;
    const bool reverse = command == browseCommand.reverse;
    if (forward || reverse) {
      *aOutAction = KeyboardScrollAction(browseCommand.scrollAction, forward);
      return true;
    }
  }

  for (const PhysicalBrowseCommand& browseCommand : physicalBrowseCommands) {
    if (command != browseCommand.command) {
      continue;
    }
    const bool forward =
        (browseCommand.direction == nsISelectionController::MOVE_RIGHT ||
         browseCommand.direction == nsISelectionController::MOVE_DOWN);

    *aOutAction = KeyboardScrollAction(browseCommand.scrollAction, forward);
    return true;
  }

  return false;
}
