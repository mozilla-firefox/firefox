/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ComposerCommandsUpdater.h"

#include "mozilla/mozalloc.h"            // for operator new
#include "mozilla/TransactionManager.h"  // for TransactionManager
#include "mozilla/dom/Selection.h"
#include "nsCommandManager.h"            // for nsCommandManager
#include "nsComponentManagerUtils.h"     // for do_CreateInstance
#include "nsDebug.h"                     // for NS_ENSURE_TRUE, etc
#include "nsDocShell.h"                  // for nsIDocShell
#include "nsError.h"                     // for NS_OK, NS_ERROR_FAILURE, etc
#include "nsID.h"                        // for NS_GET_IID, etc
#include "nsIInterfaceRequestorUtils.h"  // for do_GetInterface
#include "nsLiteralString.h"             // for NS_LITERAL_STRING
#include "nsPIDOMWindow.h"               // for nsPIDOMWindow
#include "nsPIDOMWindowInlines.h"        // for nsPIDOMWindowOuter::GetDocShell

class nsITransaction;

namespace mozilla {

ComposerCommandsUpdater::ComposerCommandsUpdater()
    : mDirtyState(eStateUninitialized),
      mSelectionCollapsed(eStateUninitialized),
      mFirstDoOfFirstUndo(true) {}

ComposerCommandsUpdater::~ComposerCommandsUpdater() {
  // cancel any outstanding update timer
  if (mUpdateTimer) {
    mUpdateTimer->Cancel();
  }
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(ComposerCommandsUpdater)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ComposerCommandsUpdater)

NS_INTERFACE_MAP_BEGIN(ComposerCommandsUpdater)
  NS_INTERFACE_MAP_ENTRY(nsITimerCallback)
  NS_INTERFACE_MAP_ENTRY(nsINamed)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsITimerCallback)
  NS_INTERFACE_MAP_ENTRIES_CYCLE_COLLECTION(ComposerCommandsUpdater)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(ComposerCommandsUpdater, mUpdateTimer, mDOMWindow,
                         mDocShell)

#if 0
#  pragma mark -
#endif

void ComposerCommandsUpdater::DidDoTransaction(
    TransactionManager& aTransactionManager) {
  // only need to update if the status of the Undo menu item changes.
  if (aTransactionManager.NumberOfUndoItems() == 1) {
    if (mFirstDoOfFirstUndo) {
      UpdateCommandGroup(CommandGroup::Undo);
    }
    mFirstDoOfFirstUndo = false;
  }
}

void ComposerCommandsUpdater::DidUndoTransaction(
    TransactionManager& aTransactionManager) {
  if (!aTransactionManager.NumberOfUndoItems()) {
    mFirstDoOfFirstUndo = true;  // reset the state for the next do
  }
  UpdateCommandGroup(CommandGroup::Undo);
}

void ComposerCommandsUpdater::DidRedoTransaction(
    TransactionManager& /*aTransactionManager*/) {
  UpdateCommandGroup(CommandGroup::Undo);
}

#if 0
#  pragma mark -
#endif

void ComposerCommandsUpdater::Init(nsPIDOMWindowOuter& aDOMWindow) {
  mDOMWindow = &aDOMWindow;
  mDocShell = aDOMWindow.GetDocShell();
}

nsresult ComposerCommandsUpdater::PrimeUpdateTimer() {
  if (!mUpdateTimer) {
    mUpdateTimer = NS_NewTimer();
  }
  const uint32_t kUpdateTimerDelay = 150;
  return mUpdateTimer->InitWithCallback(static_cast<nsITimerCallback*>(this),
                                        kUpdateTimerDelay,
                                        nsITimer::TYPE_ONE_SHOT);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY
void ComposerCommandsUpdater::TimerCallback() {
  mSelectionCollapsed = SelectionIsCollapsed();
  UpdateCommandGroup(CommandGroup::Style);
}

void ComposerCommandsUpdater::UpdateCommandGroup(CommandGroup aCommandGroup) {
  RefPtr<nsCommandManager> commandManager = GetCommandManager();
  if (NS_WARN_IF(!commandManager)) {
    return;
  }

  switch (aCommandGroup) {
    case CommandGroup::Undo:
      commandManager->CommandStatusChanged("cmd_undo");
      commandManager->CommandStatusChanged("cmd_redo");
      return;
    case CommandGroup::Style:
      commandManager->CommandStatusChanged("cmd_bold");
      commandManager->CommandStatusChanged("cmd_italic");
      commandManager->CommandStatusChanged("cmd_underline");
      commandManager->CommandStatusChanged("cmd_tt");

      commandManager->CommandStatusChanged("cmd_strikethrough");
      commandManager->CommandStatusChanged("cmd_superscript");
      commandManager->CommandStatusChanged("cmd_subscript");
      commandManager->CommandStatusChanged("cmd_nobreak");

      commandManager->CommandStatusChanged("cmd_em");
      commandManager->CommandStatusChanged("cmd_strong");
      commandManager->CommandStatusChanged("cmd_cite");
      commandManager->CommandStatusChanged("cmd_abbr");
      commandManager->CommandStatusChanged("cmd_acronym");
      commandManager->CommandStatusChanged("cmd_code");
      commandManager->CommandStatusChanged("cmd_samp");
      commandManager->CommandStatusChanged("cmd_var");

      commandManager->CommandStatusChanged("cmd_increaseFont");
      commandManager->CommandStatusChanged("cmd_decreaseFont");

      commandManager->CommandStatusChanged("cmd_paragraphState");
      commandManager->CommandStatusChanged("cmd_fontFace");
      commandManager->CommandStatusChanged("cmd_fontColor");
      commandManager->CommandStatusChanged("cmd_backgroundColor");
      commandManager->CommandStatusChanged("cmd_highlight");
      return;
    case CommandGroup::Save:
      commandManager->CommandStatusChanged("cmd_setDocumentModified");
      commandManager->CommandStatusChanged("cmd_save");
      return;
    default:
      MOZ_ASSERT_UNREACHABLE("New command group hasn't been implemented yet");
  }
}

nsresult ComposerCommandsUpdater::UpdateOneCommand(const char* aCommand) {
  RefPtr<nsCommandManager> commandManager = GetCommandManager();
  NS_ENSURE_TRUE(commandManager, NS_ERROR_FAILURE);
  commandManager->CommandStatusChanged(aCommand);
  return NS_OK;
}

bool ComposerCommandsUpdater::SelectionIsCollapsed() {
  if (NS_WARN_IF(!mDOMWindow)) {
    return true;
  }

  RefPtr<dom::Selection> domSelection = mDOMWindow->GetSelection();
  if (NS_WARN_IF(!domSelection)) {
    return false;
  }

  return domSelection->IsCollapsed();
}

nsCommandManager* ComposerCommandsUpdater::GetCommandManager() {
  if (NS_WARN_IF(!mDocShell)) {
    return nullptr;
  }
  return mDocShell->GetCommandManager();
}

NS_IMETHODIMP ComposerCommandsUpdater::GetName(nsACString& aName) {
  aName.AssignLiteral("ComposerCommandsUpdater");
  return NS_OK;
}

#if 0
#  pragma mark -
#endif

nsresult ComposerCommandsUpdater::Notify(nsITimer* aTimer) {
  NS_ASSERTION(aTimer == mUpdateTimer.get(), "Hey, this ain't my timer!");
  TimerCallback();
  return NS_OK;
}

#if 0
#  pragma mark -
#endif

}  // namespace mozilla
