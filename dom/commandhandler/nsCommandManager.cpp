/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCommandManager.h"

#include "nsCOMArray.h"
#include "nsContentUtils.h"
#include "nsIController.h"
#include "nsIControllers.h"
#include "nsIObserver.h"
#include "nsPIDOMWindow.h"
#include "nsPIWindowRoot.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"

nsCommandManager::nsCommandManager(mozIDOMWindowProxy* aWindow)
    : mWindow(aWindow) {
  MOZ_DIAGNOSTIC_ASSERT(mWindow);
}

nsCommandManager::~nsCommandManager() = default;

NS_IMPL_CYCLE_COLLECTION_CLASS(nsCommandManager)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsCommandManager)
  tmp->mObserversTable.Clear();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsCommandManager)
  for (const auto& entry : tmp->mObserversTable) {
    nsCommandManager::ObserverList* observers = entry.GetRawPtr();
    int32_t numItems = observers->Length();
    for (int32_t i = 0; i < numItems; ++i) {
      cb.NoteXPCOMChild(observers->ElementAt(i));
    }
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsCommandManager)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsCommandManager)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsCommandManager)
  NS_INTERFACE_MAP_ENTRY(nsICommandManager)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICommandManager)
NS_INTERFACE_MAP_END

void nsCommandManager::CommandStatusChanged(const char* aCommandName) {
  ObserverList* commandObservers;
  mObserversTable.Get(aCommandName, &commandObservers);

  if (commandObservers) {
    // XXX Should we worry about observers removing themselves from Observe()?
    int32_t i, numItems = commandObservers->Length();
    for (i = 0; i < numItems; ++i) {
      nsCOMPtr<nsIObserver> observer = commandObservers->ElementAt(i);
      // should we get the command state to pass here? This might be expensive.
      observer->Observe(NS_ISUPPORTS_CAST(nsICommandManager*, this),
                        aCommandName, u"command_status_changed");
    }
  }
}

#if 0
#  pragma mark -
#endif

NS_IMETHODIMP
nsCommandManager::AddCommandObserver(nsIObserver* aCommandObserver,
                                     const char* aCommandToObserve) {
  NS_ENSURE_ARG(aCommandObserver);

  // XXX todo: handle special cases of aCommandToObserve being null, or empty

  // for each command in the table, we make a list of observers for that command
  auto* const commandObservers =
      mObserversTable.GetOrInsertNew(aCommandToObserve);

  // need to check that this command observer hasn't already been registered
  int32_t existingIndex = commandObservers->IndexOf(aCommandObserver);
  if (existingIndex == -1) {
    commandObservers->AppendElement(aCommandObserver);
  } else {
    NS_WARNING("Registering command observer twice on the same command");
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCommandManager::RemoveCommandObserver(nsIObserver* aCommandObserver,
                                        const char* aCommandObserved) {
  NS_ENSURE_ARG(aCommandObserver);

  // XXX todo: handle special cases of aCommandToObserve being null, or empty

  ObserverList* commandObservers;
  if (!mObserversTable.Get(aCommandObserved, &commandObservers)) {
    return NS_ERROR_UNEXPECTED;
  }

  commandObservers->RemoveElement(aCommandObserver);

  return NS_OK;
}

NS_IMETHODIMP
nsCommandManager::IsCommandSupported(const char* aCommandName,
                                     mozIDOMWindowProxy* aTargetWindow,
                                     bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  nsCOMPtr<nsIController> controller;
  GetControllerForCommand(aCommandName, aTargetWindow,
                          getter_AddRefs(controller));
  *aResult = (controller.get() != nullptr);
  return NS_OK;
}

NS_IMETHODIMP
nsCommandManager::IsCommandEnabled(const char* aCommandName,
                                   mozIDOMWindowProxy* aTargetWindow,
                                   bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  if (!aCommandName) {
    *aResult = false;
    return NS_OK;
  }
  *aResult = IsCommandEnabled(nsDependentCString(aCommandName), aTargetWindow);
  return NS_OK;
}

bool nsCommandManager::IsCommandEnabled(const nsCString& aCommandName,
                                        mozIDOMWindowProxy* aTargetWindow) {
  nsCOMPtr<nsIController> controller;
  GetControllerForCommand(aCommandName.get(), aTargetWindow,
                          getter_AddRefs(controller));
  if (!controller) {
    return false;
  }

  bool enabled = false;
  controller->IsCommandEnabled(aCommandName.get(), &enabled);
  return enabled;
}

NS_IMETHODIMP
nsCommandManager::GetCommandState(const char* aCommandName,
                                  mozIDOMWindowProxy* aTargetWindow,
                                  nsICommandParams* aCommandParams) {
  nsCOMPtr<nsIController> controller;
  nsAutoString tValue;
  nsresult rv = GetControllerForCommand(aCommandName, aTargetWindow,
                                        getter_AddRefs(controller));
  if (!controller) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsICommandController> commandController =
      do_QueryInterface(controller);
  if (commandController) {
    rv = commandController->GetCommandStateWithParams(aCommandName,
                                                      aCommandParams);
  } else {
    rv = NS_ERROR_NOT_IMPLEMENTED;
  }
  return rv;
}

NS_IMETHODIMP
nsCommandManager::DoCommand(const char* aCommandName,
                            nsICommandParams* aCommandParams,
                            mozIDOMWindowProxy* aTargetWindow) {
  nsCOMPtr<nsIController> controller;
  nsresult rv = GetControllerForCommand(aCommandName, aTargetWindow,
                                        getter_AddRefs(controller));
  if (!controller) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsICommandController> commandController =
      do_QueryInterface(controller);
  if (commandController && aCommandParams) {
    rv = commandController->DoCommandWithParams(aCommandName, aCommandParams);
  } else {
    rv = controller->DoCommand(aCommandName);
  }
  return rv;
}

nsresult nsCommandManager::GetControllerForCommand(
    const char* aCommand, mozIDOMWindowProxy* aTargetWindow,
    nsIController** aResult) {
  nsresult rv = NS_ERROR_FAILURE;
  *aResult = nullptr;

  // check if we're in content or chrome
  // if we're not chrome we must have a target window or we bail
  if (!nsContentUtils::LegacyIsCallerChromeOrNativeCode()) {
    if (!aTargetWindow) {
      return rv;
    }

    // if a target window is specified, it must be the window we expect
    if (aTargetWindow != mWindow) {
      return NS_ERROR_FAILURE;
    }
  }

  if (auto* targetWindow = nsPIDOMWindowOuter::From(aTargetWindow)) {
    // get the controller for this particular window
    nsCOMPtr<nsIControllers> controllers;
    rv = targetWindow->GetControllers(getter_AddRefs(controllers));
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (!controllers) {
      return NS_ERROR_FAILURE;
    }

    // dispatch the command
    return controllers->GetControllerForCommand(aCommand, aResult);
  }

  auto* window = nsPIDOMWindowOuter::From(mWindow);
  NS_ENSURE_TRUE(window, NS_ERROR_FAILURE);
  nsCOMPtr<nsPIWindowRoot> root = window->GetTopWindowRoot();
  NS_ENSURE_TRUE(root, NS_ERROR_FAILURE);

  // no target window; send command to focus controller
  return root->GetControllerForCommand(aCommand, false /* for any window */,
                                       aResult);
}
