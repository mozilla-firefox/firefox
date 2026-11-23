/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindowRoot.h"

#include "mozilla/BasicEvents.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLTextAreaElement.h"
#include "mozilla/dom/JSActorService.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/WindowRootBinding.h"
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsFocusManager.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsFrameSelection.h"
#include "nsGlobalWindowInner.h"
#include "nsGlobalWindowOuter.h"
#include "nsIContent.h"
#include "nsIController.h"
#include "nsIControllers.h"
#include "nsPIDOMWindow.h"
#include "nsPresContext.h"
#include "nsQueryActor.h"
#include "nsQueryObject.h"
#include "nsString.h"
#include "nsXULElement.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::dom;

nsWindowRoot::nsWindowRoot(nsPIDOMWindowOuter* aWindow) {
  SetIsOnMainThread();
  mWindow = aWindow;
}

nsWindowRoot::~nsWindowRoot() {
  if (mListenerManager) {
    mListenerManager->Disconnect();
  }

  JSActorService::UnregisterChromeEventTarget(this);
}

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(nsWindowRoot)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsWindowRoot)
  JSActorService::UnregisterChromeEventTarget(tmp);

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWindow)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mListenerManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_PRESERVED_WRAPPER
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsWindowRoot)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWindow)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mListenerManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsWindowRoot)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsPIWindowRoot)
  NS_INTERFACE_MAP_ENTRY(mozilla::dom::EventTarget)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsWindowRoot)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsWindowRoot)

bool nsWindowRoot::DispatchEvent(Event& aEvent, CallerType aCallerType,
                                 ErrorResult& aRv) {
  nsEventStatus status = nsEventStatus_eIgnore;
  nsresult rv = EventDispatcher::DispatchDOMEvent(
      static_cast<EventTarget*>(this), nullptr, &aEvent, nullptr, &status);
  bool retval = !aEvent.DefaultPrevented(aCallerType);
  if (NS_FAILED(rv)) {
    aRv.Throw(rv);
  }
  return retval;
}

bool nsWindowRoot::ComputeDefaultWantsUntrusted(ErrorResult& aRv) {
  return false;
}

EventListenerManager* nsWindowRoot::GetOrCreateListenerManager() {
  if (!mListenerManager) {
    mListenerManager =
        new EventListenerManager(static_cast<EventTarget*>(this));
  }

  return mListenerManager;
}

EventListenerManager* nsWindowRoot::GetExistingListenerManager() const {
  return mListenerManager;
}

void nsWindowRoot::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mCanHandle = true;
  aVisitor.mForceContentDispatch = true;  // FIXME! Bug 329119
  aVisitor.SetParentTarget(mParent, false);

  // We need to handle eFocus and eBlur to set up selection. However, that
  // needs to be done before dispatching the event to the DOM:
  // https://w3c.github.io/uievents/#event-type-focus
  // > The focus MUST be given to the element before the dispatch of this
  // > event type.
  // https://w3c.github.io/uievents/#event-type-blur
  // > The focus MUST be taken from the element before the dispatch of this
  // > event type.
  // Therefore, we want the pre handler.
  if (aVisitor.mEvent->IsTrusted() && (aVisitor.mEvent->mMessage == eFocus ||
                                       aVisitor.mEvent->mMessage == eBlur)) {
    aVisitor.mWantsPreHandleEvent = true;
  }
}

nsresult nsWindowRoot::PreHandleEvent(EventChainVisitor& aVisitor) {
  MOZ_ASSERT(aVisitor.mEvent->mMessage == eFocus ||
             aVisitor.mEvent->mMessage == eBlur);
  MOZ_ASSERT(aVisitor.mEvent->IsTrusted());

  const nsCOMPtr<nsINode> targetNode = nsINode::FromEventTargetOrNull(
      aVisitor.mEvent->GetOriginalDOMEventTarget());
  if (!targetNode) {
    return NS_OK;
  }

  const RefPtr<PresShell> presShell = targetNode->OwnerDoc()->GetPresShell();
  if (!presShell) [[unlikely]] {
    return NS_OK;
  }

  // Maintain selection state before HTMLEditor and/or TextEditor handles
  // focus/blur below.
  if (Document* const targetDocument = Document::FromNodeOrNull(targetNode)) {
    if (aVisitor.mEvent->mMessage == eFocus) {
      nsFrameSelection::WillFocusDocument(*presShell, *targetDocument);
    } else {
      nsFrameSelection::WillBlurDocument(*presShell, *targetDocument);
    }
  }
  // If new focused element is a text control element in an editing host,
  // both HTMLEditor and TextEditor need to handle the focus event for the
  // backward compatibility. Then, we need to make HTMLEditor handle it
  // first, then TextEditor will handle it below via
  // TextControlElement::WillFocus().
  if (aVisitor.mEvent->mMessage == eFocus) {
    HTMLEditor::WillFocusNode(*presShell, targetNode);
  }
  // To keep the traditional behavior at blur, we should call
  // HTMLEditor::OnBlur() before TextEditor::OnBlur() which is now called by
  // TextControlElement::WillBlur() called below.
  else {
    HTMLEditor::WillBlurNode(*presShell, targetNode);
  }

  // Finally, set up selection of `TextEditor` if and only if the event target
  // is a text control.
  if (auto* const targetTextControlElement =
          TextControlElement::FromNodeOrNull(targetNode)) {
    if (targetTextControlElement->IsSingleLineTextControlOrTextArea()) {
      if (aVisitor.mEvent->mMessage == eFocus) {
        MOZ_KnownLive(targetTextControlElement)->WillFocus(*aVisitor.mEvent);
      } else {
        MOZ_KnownLive(targetTextControlElement)->WillBlur(*aVisitor.mEvent);
      }
    }
  }
  return NS_OK;
}

nsGlobalWindowInner* nsWindowRoot::GetInnerWindow() {
  return nsGlobalWindowInner::Cast(mWindow->GetCurrentInnerWindow());
}

nsIGlobalObject* nsWindowRoot::GetRelevantGlobal() const {
  return nsGlobalWindowInner::Cast(mWindow->GetCurrentInnerWindow());
}

nsPIDOMWindowOuter* nsWindowRoot::GetWindow() { return mWindow; }

nsresult nsWindowRoot::GetControllers(bool aForVisibleWindow,
                                      nsIControllers** aResult) {
  *aResult = nullptr;

  // XXX: we should fix this so there's a generic interface that
  // describes controllers, so this code would have no special
  // knowledge of what object might have controllers.

  nsFocusManager::SearchRange searchRange =
      aForVisibleWindow ? nsFocusManager::eIncludeVisibleDescendants
                        : nsFocusManager::eIncludeAllDescendants;
  nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
  nsIContent* focusedContent = nsFocusManager::GetFocusedDescendant(
      mWindow, searchRange, getter_AddRefs(focusedWindow));
  if (focusedContent) {
    if (auto* xulElement = nsXULElement::FromNode(focusedContent)) {
      *aResult = xulElement->GetExtantControllers();
      NS_IF_ADDREF(*aResult);
      return NS_OK;
    }
    auto* htmlTextArea = HTMLTextAreaElement::FromNode(focusedContent);
    if (htmlTextArea) {
      return htmlTextArea->GetControllers(aResult);
    }
    auto* htmlInputElement = HTMLInputElement::FromNode(focusedContent);
    if (htmlInputElement) {
      return htmlInputElement->GetControllers(aResult);
    }
    if (focusedContent->IsEditable() && focusedWindow) {
      return focusedWindow->GetControllers(aResult);
    }
  } else {
    return focusedWindow->GetControllers(aResult);
  }

  return NS_OK;
}

nsresult nsWindowRoot::GetControllerForCommand(const char* aCommand,
                                               bool aForVisibleWindow,
                                               nsIController** _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = nullptr;

  // If this is the parent process, check if a child browsing context from
  // another process is focused, and ask if it has a controller actor that
  // supports the command.
  if (XRE_IsParentProcess()) {
    nsFocusManager* fm = nsFocusManager::GetFocusManager();
    if (!fm) {
      return NS_ERROR_FAILURE;
    }

    // Unfortunately, messages updating the active/focus state in the focus
    // manager don't happen fast enough in the case when switching focus between
    // processes when clicking on a chrome UI element while a child tab is
    // focused, so we need to check whether the focus manager thinks a child
    // frame is focused as well.
    nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
    nsIContent* focusedContent = nsFocusManager::GetFocusedDescendant(
        mWindow, nsFocusManager::eIncludeAllDescendants,
        getter_AddRefs(focusedWindow));
    RefPtr<nsFrameLoaderOwner> loaderOwner = do_QueryObject(focusedContent);
    if (loaderOwner) {
      // Only check browsing contexts if a remote frame is focused. If chrome is
      // focused, just check the controllers directly below.
      RefPtr<nsFrameLoader> frameLoader = loaderOwner->GetFrameLoader();
      if (frameLoader && frameLoader->IsRemoteFrame()) {
        // The focused browsing context points at the tab that last held content
        // focus, even if the window in question isn't frontmost or the app
        // isn't the active app at all. Use it so context-menu commands work on
        // a background window.
        BrowsingContext* focusedBC = fm->GetFocusedBrowsingContextInChrome();
        if (focusedBC) {
          // At this point, it is known that a child process is focused, so ask
          // its Controllers actor if the command is supported.
          nsCOMPtr<nsIController> controller = do_QueryActor(
              "Controllers", focusedBC->Canonical()->GetCurrentWindowGlobal());
          if (controller) {
            bool supported;
            controller->SupportsCommand(aCommand, &supported);
            if (supported) {
              controller.forget(_retval);
              return NS_OK;
            }
          }
        }
      }
    }
  }

  {
    nsCOMPtr<nsIControllers> controllers;
    GetControllers(aForVisibleWindow, getter_AddRefs(controllers));
    if (controllers) {
      nsCOMPtr<nsIController> controller;
      controllers->GetControllerForCommand(aCommand,
                                           getter_AddRefs(controller));
      if (controller) {
        controller.forget(_retval);
        return NS_OK;
      }
    }
  }

  nsFocusManager::SearchRange searchRange =
      aForVisibleWindow ? nsFocusManager::eIncludeVisibleDescendants
                        : nsFocusManager::eIncludeAllDescendants;
  nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
  nsFocusManager::GetFocusedDescendant(mWindow, searchRange,
                                       getter_AddRefs(focusedWindow));
  while (focusedWindow) {
    nsCOMPtr<nsIControllers> controllers;
    focusedWindow->GetControllers(getter_AddRefs(controllers));
    if (controllers) {
      nsCOMPtr<nsIController> controller;
      controllers->GetControllerForCommand(aCommand,
                                           getter_AddRefs(controller));
      if (controller) {
        controller.forget(_retval);
        return NS_OK;
      }
    }

    // XXXndeakin P3 is this casting safe?
    nsGlobalWindowOuter* win = nsGlobalWindowOuter::Cast(focusedWindow);
    focusedWindow = win->GetPrivateParent();
  }

  return NS_OK;
}

void nsWindowRoot::GetEnabledDisabledCommandsForControllers(
    nsIControllers* aControllers, nsTHashSet<nsCString>& aCommandsHandled,
    nsTArray<nsCString>& aEnabledCommands,
    nsTArray<nsCString>& aDisabledCommands) {
  uint32_t controllerCount;
  aControllers->GetControllerCount(&controllerCount);
  for (uint32_t c = 0; c < controllerCount; c++) {
    nsCOMPtr<nsIController> controller;
    aControllers->GetControllerAt(c, getter_AddRefs(controller));

    nsCOMPtr<nsICommandController> commandController(
        do_QueryInterface(controller));
    if (commandController) {
      // All of our default command controllers have 20-60 commands.  Let's just
      // leave enough space here for all of them so we probably don't need to
      // heap-allocate.
      AutoTArray<nsCString, 64> commands;
      if (NS_SUCCEEDED(commandController->GetSupportedCommands(commands))) {
        for (auto& commandStr : commands) {
          // Use a hash to determine which commands have already been handled by
          // earlier controllers, as the earlier controller's result should get
          // priority.
          if (aCommandsHandled.EnsureInserted(commandStr)) {
            // We inserted a new entry into aCommandsHandled.
            bool enabled = false;
            controller->IsCommandEnabled(commandStr.get(), &enabled);

            if (enabled) {
              aEnabledCommands.AppendElement(commandStr);
            } else {
              aDisabledCommands.AppendElement(commandStr);
            }
          }
        }
      }
    }
  }
}

void nsWindowRoot::GetEnabledDisabledCommands(
    nsTArray<nsCString>& aEnabledCommands,
    nsTArray<nsCString>& aDisabledCommands) {
  nsTHashSet<nsCString> commandsHandled;

  nsCOMPtr<nsIControllers> controllers;
  GetControllers(false, getter_AddRefs(controllers));
  if (controllers) {
    GetEnabledDisabledCommandsForControllers(
        controllers, commandsHandled, aEnabledCommands, aDisabledCommands);
  }

  nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
  nsFocusManager::GetFocusedDescendant(mWindow,
                                       nsFocusManager::eIncludeAllDescendants,
                                       getter_AddRefs(focusedWindow));
  while (focusedWindow) {
    focusedWindow->GetControllers(getter_AddRefs(controllers));
    if (controllers) {
      GetEnabledDisabledCommandsForControllers(
          controllers, commandsHandled, aEnabledCommands, aDisabledCommands);
    }

    nsGlobalWindowOuter* win = nsGlobalWindowOuter::Cast(focusedWindow);
    focusedWindow = win->GetPrivateParent();
  }
}

already_AddRefed<nsINode> nsWindowRoot::GetPopupNode() {
  nsCOMPtr<nsINode> popupNode = do_QueryReferent(mPopupNode);
  return popupNode.forget();
}

void nsWindowRoot::SetPopupNode(nsINode* aNode) {
  mPopupNode = do_GetWeakReference(aNode);
}

nsIGlobalObject* nsWindowRoot::GetParentObject() {
  return xpc::NativeGlobal(xpc::PrivilegedJunkScope());
}

JSObject* nsWindowRoot::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return mozilla::dom::WindowRoot_Binding::Wrap(aCx, this, aGivenProto);
}

void nsWindowRoot::AddBrowser(nsIRemoteTab* aBrowser) {
  nsWeakPtr weakBrowser = do_GetWeakReference(aBrowser);
  mWeakBrowsers.Insert(weakBrowser);
}

void nsWindowRoot::RemoveBrowser(nsIRemoteTab* aBrowser) {
  nsWeakPtr weakBrowser = do_GetWeakReference(aBrowser);
  mWeakBrowsers.Remove(weakBrowser);
}

void nsWindowRoot::EnumerateBrowsers(BrowserEnumerator aEnumFunc, void* aArg) {
  // Collect strong references to all browsers in a separate array in
  // case aEnumFunc alters mWeakBrowsers.
  nsTArray<nsCOMPtr<nsIRemoteTab>> remoteTabs;
  for (const auto& key : mWeakBrowsers) {
    nsCOMPtr<nsIRemoteTab> remoteTab(do_QueryReferent(key));
    if (remoteTab) {
      remoteTabs.AppendElement(remoteTab);
    }
  }

  for (uint32_t i = 0; i < remoteTabs.Length(); ++i) {
    aEnumFunc(remoteTabs[i], aArg);
  }
}

///////////////////////////////////////////////////////////////////////////////////

already_AddRefed<EventTarget> NS_NewWindowRoot(nsPIDOMWindowOuter* aWindow) {
  nsCOMPtr<EventTarget> result = new nsWindowRoot(aWindow);

  RefPtr<JSActorService> wasvc = JSActorService::GetSingleton();
  wasvc->RegisterChromeEventTarget(result);

  return result.forget();
}
