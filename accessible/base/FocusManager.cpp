/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FocusManager.h"

#include "DocAccessible-inl.h"
#include "LocalAccessible-inl.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/a11y/DocAccessibleParent.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/WindowContext.h"
#include "nsAccessibilityService.h"
#include "nsEventShell.h"
#include "nsFocusManager.h"
#include "nsLayoutUtils.h"

namespace mozilla {
namespace a11y {

/**
 * If our focus target is the body accessible, manually retarget it to the doc
 * accessible. Otherwise, focus will land on what is effectively just a
 * generic accessible as far as clients are concerned, which doesn't have
 * any useful semantics.
 */
static LocalAccessible* MaybeAdjustDocumentFocusTarget(
    LocalAccessible* aTarget) {
  if (!aTarget) {
    return aTarget;
  }
  DocAccessible* document = aTarget->Document();
  if (document && document->IsBodyElement(aTarget->GetContent())) {
    return document;
  }
  return aTarget;
}

/**
 * Return the appropriate accessible target given the focused node.
 */
static LocalAccessible* FocusTargetFor(DocAccessible* aDocument,
                                       nsINode* aNode) {
  return MaybeAdjustDocumentFocusTarget(
      aDocument->GetAccessibleEvenIfNotInMapOrContainer(aNode));
}

FocusManager::FocusManager() = default;

FocusManager::~FocusManager() = default;

LocalAccessible* FocusManager::FocusedLocalAccessible() const {
  MOZ_ASSERT(NS_IsMainThread());
  if (mActiveItem) {
    if (mActiveItem->IsDefunct()) {
      MOZ_ASSERT_UNREACHABLE("Stored active item is unbound from document");
      return nullptr;
    }

    return mActiveItem;
  }

  if (nsAccessibilityService::IsShutdown()) {
    // We might try to get or create a DocAccessible below, which isn't safe (or
    // useful) if the accessibility service is shutting down.
    return nullptr;
  }

  nsINode* focusedNode = FocusedDOMNode();
  if (focusedNode) {
    DocAccessible* doc =
        GetAccService()->GetDocAccessible(focusedNode->OwnerDoc());
    return doc ? FocusTargetFor(doc, focusedNode) : nullptr;
  }

  return nullptr;
}

Accessible* FocusManager::FocusedAccessible() const {
#if defined(ANDROID)
  // It's not safe to call FocusedLocalAccessible() except on the main thread.
  // Android might query RemoteAccessibles on the UI thread, which might call
  // FocusedAccessible(). Never try to get the focused LocalAccessible in this
  // case.
  if (NS_IsMainThread()) {
    if (Accessible* focusedAcc = FocusedLocalAccessible()) {
      return focusedAcc;
    }
  } else {
    nsAccessibilityService::GetAndroidMonitor().AssertCurrentThreadOwns();
  }
  return mFocusedRemoteDoc ? mFocusedRemoteDoc->GetFocusedAcc() : nullptr;
#else
  if (Accessible* focusedAcc = FocusedLocalAccessible()) {
    return focusedAcc;
  }

  if (!XRE_IsParentProcess()) {
    // DocAccessibleParent's don't exist in the content
    // process, so we can't return anything useful if this
    // is the case.
    return nullptr;
  }

  nsFocusManager* focusManagerDOM = nsFocusManager::GetFocusManager();
  if (!focusManagerDOM) {
    return nullptr;
  }

  // If we call GetFocusedBrowsingContext from the chrome process
  // it returns the BrowsingContext for the focused _window_, which
  // is not helpful here. Instead use GetFocusedBrowsingContextInChrome
  // which returns the content BrowsingContext that has focus.
  dom::BrowsingContext* focusedContext =
      focusManagerDOM->GetFocusedBrowsingContextInChrome();
  dom::WindowContext* focusedWindow =
      focusedContext ? focusedContext->GetCurrentWindowContext() : nullptr;

  DocAccessibleParent* focusedDoc = DocAccessibleParent::GetFrom(focusedWindow);
  return focusedDoc ? focusedDoc->GetFocusedAcc() : nullptr;
#endif  // defined(ANDROID)
}

bool FocusManager::IsFocusWithin(const Accessible* aContainer) const {
  Accessible* child = FocusedAccessible();
  while (child) {
    if (child == aContainer) return true;

    child = child->Parent();
  }
  return false;
}

FocusManager::FocusDisposition FocusManager::IsInOrContainsFocus(
    const LocalAccessible* aAccessible) const {
  LocalAccessible* focus = FocusedLocalAccessible();
  if (!focus) return eNone;

  // If focused.
  if (focus == aAccessible) return eFocused;

  // If contains the focus.
  LocalAccessible* child = focus->LocalParent();
  while (child) {
    if (child == aAccessible) return eContainsFocus;

    child = child->LocalParent();
  }

  // If contained by focus.
  child = aAccessible->LocalParent();
  while (child) {
    if (child == focus) return eContainedByFocus;

    child = child->LocalParent();
  }

  return eNone;
}

bool FocusManager::WasLastFocused(const LocalAccessible* aAccessible) const {
  return mLastFocus == aAccessible;
}

void FocusManager::NotifyOfDOMFocus(nsISupports* aTarget) {
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eFocus)) {
    logging::FocusNotificationTarget("DOM focus", "Target", aTarget);
  }
#endif

  mActiveItem = nullptr;

  nsCOMPtr<nsINode> targetNode(do_QueryInterface(aTarget));
  if (targetNode) {
    DocAccessible* document =
        GetAccService()->GetDocAccessible(targetNode->OwnerDoc());
    if (document) {
      // Set selection listener for focused element.
      if (targetNode->IsElement()) {
        SelectionMgr()->SetControlSelectionListener(targetNode->AsElement());
      }

      document->HandleNotification<FocusManager, nsINode>(
          this, &FocusManager::ProcessDOMFocus, targetNode);
    }
  }
}

void FocusManager::NotifyOfDOMBlur(nsISupports* aTarget) {
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eFocus)) {
    logging::FocusNotificationTarget("DOM blur", "Target", aTarget);
  }
#endif

  mActiveItem = nullptr;

  // If DOM document stays focused then fire accessible focus event to process
  // the case when no element within this DOM document will be focused.
  nsCOMPtr<nsINode> targetNode(do_QueryInterface(aTarget));
  if (targetNode && targetNode->OwnerDoc() == FocusedDOMDocument()) {
    dom::Document* DOMDoc = targetNode->OwnerDoc();
    DocAccessible* document = GetAccService()->GetDocAccessible(DOMDoc);
    if (document) {
      // Clear selection listener for previously focused element.
      if (targetNode->IsElement()) {
        SelectionMgr()->ClearControlSelectionListener();
      }

      document->HandleNotification<FocusManager, nsINode>(
          this, &FocusManager::ProcessDOMFocus, DOMDoc);
    }
  }
}

void FocusManager::ActiveItemChanged(LocalAccessible* aItem,
                                     bool aCheckIfActive) {
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eFocus)) {
    logging::FocusNotificationTarget("active item changed", "Item", aItem);
  }
#endif

  // Nothing changed, happens for XUL trees and HTML selects.
  if (aItem && aItem == mActiveItem) {
    return;
  }

  if (aItem && aCheckIfActive) {
    LocalAccessible* widget = aItem->ContainerWidget();
#ifdef A11Y_LOG
    if (logging::IsEnabled(logging::eFocus)) logging::ActiveWidget(widget);
#endif
    if (!widget || !widget->IsActiveWidget() || !widget->AreItemsOperable()) {
      return;
    }
  }
  mActiveItem = aItem;

  // If mActiveItem is null we may need to shift a11y focus back to a remote
  // document. For example, when combobox popup is closed, then
  // the focus should be moved back to the combobox.
  if (!mActiveItem && XRE_IsParentProcess()) {
    dom::BrowserParent* browser = dom::BrowserParent::GetFocused();
    if (browser) {
      a11y::DocAccessibleParent* dap = browser->GetTopLevelDocAccessible();
      if (dap) {
        (void)dap->SendRestoreFocus();
      }
    }
  }

  // If active item is changed then fire accessible focus event on it, otherwise
  // if there's no an active item then fire focus event to accessible having
  // DOM focus.
  LocalAccessible* target = FocusedLocalAccessible();
  if (target) {
    DispatchFocusEvent(target->Document(), target);
  }
}

void FocusManager::ForceFocusEvent() {
  nsINode* focusedNode = FocusedDOMNode();
  if (focusedNode) {
    DocAccessible* document =
        GetAccService()->GetDocAccessible(focusedNode->OwnerDoc());
    if (document) {
      document->HandleNotification<FocusManager, nsINode>(
          this, &FocusManager::ProcessDOMFocus, focusedNode);
    }
  }
}

void FocusManager::DispatchFocusEvent(DocAccessible* aDocument,
                                      LocalAccessible* aTarget) {
  MOZ_ASSERT(aDocument, "No document for focused accessible!");
  aTarget = MaybeAdjustDocumentFocusTarget(aTarget);
  if (aDocument) {
    auto event =
        MakeRefPtr<AccEvent>(nsIAccessibleEvent::EVENT_FOCUS, aTarget,
                             eAutoDetect, AccEvent::eCoalesceOfSameType);
    aDocument->FireDelayedEvent(event);
    mLastFocus = aTarget;
    if (mActiveItem != aTarget) {
      // This new focus overrides the stored active item, so clear the active
      // item. Among other things, the old active item might die.
      mActiveItem = nullptr;
    }

#ifdef A11Y_LOG
    if (logging::IsEnabled(logging::eFocus)) logging::FocusDispatched(aTarget);
#endif
  }
}

void FocusManager::ProcessDOMFocus(nsINode* aTarget) {
#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eFocus)) {
    logging::FocusNotificationTarget("process DOM focus", "Target", aTarget);
  }
#endif

  DocAccessible* document =
      GetAccService()->GetDocAccessible(aTarget->OwnerDoc());
  if (!document) return;

  LocalAccessible* target = FocusTargetFor(document, aTarget);
  if (target) {
    // Check if still focused. Otherwise we can end up with storing the active
    // item for control that isn't focused anymore.
    nsINode* focusedNode = FocusedDOMNode();
    if (!focusedNode) return;

    LocalAccessible* DOMFocus = FocusTargetFor(document, focusedNode);
    if (target != DOMFocus) return;

    LocalAccessible* activeItem = target->CurrentItem();
    if (activeItem) {
      mActiveItem = activeItem;
      target = activeItem;
    }

    DispatchFocusEvent(document, target);
  }
}

void FocusManager::ProcessFocusEvent(AccEvent* aEvent) {
  MOZ_ASSERT(aEvent->GetEventType() == nsIAccessibleEvent::EVENT_FOCUS,
             "Focus event is expected!");

  // Emit focus event if event target is the active item. Otherwise then check
  // if it's still focused and then update active item and emit focus event.
  LocalAccessible* target =
      MaybeAdjustDocumentFocusTarget(aEvent->GetAccessible());
  MOZ_ASSERT(!target->IsDefunct());
  if (target != mActiveItem) {
    // Check if still focused. Otherwise we can end up with storing the active
    // item for control that isn't focused anymore.
    DocAccessible* document = aEvent->Document();
    nsINode* focusedNode = FocusedDOMNode();
    if (!focusedNode) return;

    LocalAccessible* DOMFocus = FocusTargetFor(document, focusedNode);
    if (target != DOMFocus) return;

    LocalAccessible* activeItem = target->CurrentItem();
    if (activeItem) {
      mActiveItem = activeItem;
      target = activeItem;
      MOZ_ASSERT(!target->IsDefunct());
    }
  }

  // Fire menu start/end events for ARIA menus.
  if (target->IsARIARole(nsGkAtoms::menuitem)) {
    // The focus was moved into menu.
    LocalAccessible* ARIAMenubar = nullptr;
    for (LocalAccessible* parent = target->LocalParent(); parent;
         parent = parent->LocalParent()) {
      if (parent->IsARIARole(nsGkAtoms::menubar)) {
        ARIAMenubar = parent;
        break;
      }

      // Go up in the parent chain of the menu hierarchy.
      if (!parent->IsARIARole(nsGkAtoms::menuitem) &&
          !parent->IsARIARole(nsGkAtoms::menu)) {
        break;
      }
    }

    if (ARIAMenubar != mActiveARIAMenubar) {
      // Leaving ARIA menu. Fire menu_end event on current menubar.
      if (mActiveARIAMenubar) {
        auto menuEndEvent =
            MakeRefPtr<AccEvent>(nsIAccessibleEvent::EVENT_MENU_END,
                                 mActiveARIAMenubar, aEvent->FromUserInput());
        nsEventShell::FireEvent(menuEndEvent);
      }

      mActiveARIAMenubar = ARIAMenubar;

      // Entering ARIA menu. Fire menu_start event.
      if (mActiveARIAMenubar) {
        auto menuStartEvent =
            MakeRefPtr<AccEvent>(nsIAccessibleEvent::EVENT_MENU_START,
                                 mActiveARIAMenubar, aEvent->FromUserInput());
        nsEventShell::FireEvent(menuStartEvent);
      }
    }
  } else if (mActiveARIAMenubar) {
    // Focus left a menu. Fire menu_end event.
    auto menuEndEvent =
        MakeRefPtr<AccEvent>(nsIAccessibleEvent::EVENT_MENU_END,
                             mActiveARIAMenubar, aEvent->FromUserInput());
    nsEventShell::FireEvent(menuEndEvent);

    mActiveARIAMenubar = nullptr;
  }

#ifdef A11Y_LOG
  if (logging::IsEnabled(logging::eFocus)) {
    logging::FocusNotificationTarget("fire focus event", "Target", target);
  }
#endif

  // Reset cached caret value. The cache will be updated upon processing the
  // next caret move event. This ensures that we will return the correct caret
  // offset before the caret move event is handled.
  SelectionMgr()->ResetCaretOffset();

  auto focusEvent = MakeRefPtr<AccEvent>(nsIAccessibleEvent::EVENT_FOCUS,
                                         target, aEvent->FromUserInput());
  nsEventShell::FireEvent(focusEvent);

  if (NS_WARN_IF(target->IsDefunct())) {
    // target died during nsEventShell::FireEvent.
    return;
  }

  // An anchor jump may have occurred while the document was not focused. This
  // could even be before the document was first ever focused. Process it now.
  DocAccessible* targetDocument = target->Document();
  if (!targetDocument->ProcessAnchorJump()) {
    // The anchor jump was ignored due to the focus. That means that this focus
    // change overrides the anchor jump, so clear the jump.
    targetDocument->SetAnchorJump(nullptr);
  }
}

nsINode* FocusManager::FocusedDOMNode() const {
  nsFocusManager* DOMFocusManager = nsFocusManager::GetFocusManager();
  nsIContent* focusedElm = DOMFocusManager->GetFocusedElement();
  nsIFrame* focusedFrame =
      focusedElm ? nsLayoutUtils::GetPrimaryOrAreaImageFrame(focusedElm)
                 : nullptr;
  // DOM elements retain their focused state when they get styled as display:
  // none/content or visibility: hidden. We should treat those cases as if those
  // elements were removed, and focus on doc.
  if (focusedFrame && focusedFrame->StyleVisibility()->IsVisible()) {
    // Print preview documents don't get DocAccessibles, but we still want a11y
    // focus to go somewhere useful. Therefore, we allow a11y focus to land on
    // the OuterDocAccessible in this case.
    // Note that this code only handles remote print preview documents.
    if (EventStateManager::IsTopLevelRemoteTarget(focusedElm) &&
        focusedElm->AsElement()->HasAttribute(u"printpreview"_ns)) {
      return focusedElm;
    }
    // No focus on remote target elements like xul:browser having DOM focus and
    // residing in chrome process because it means an element in content process
    // keeps the focus. Similarly, suppress focus on OOP iframes because an
    // element in another content process should now have the focus.
    if (EventStateManager::IsRemoteTarget(focusedElm)) {
      return nullptr;
    }
    return focusedElm;
  }

  // Otherwise the focus can be on DOM document.
  dom::BrowsingContext* context = DOMFocusManager->GetFocusedBrowsingContext();
  if (context) {
    // GetDocShell will return null if the document isn't in our process.
    nsIDocShell* shell = context->GetDocShell();
    if (shell) {
      return shell->GetDocument();
    }
  }

  // Focus isn't in this process.
  return nullptr;
}

dom::Document* FocusManager::FocusedDOMDocument() const {
  nsINode* focusedNode = FocusedDOMNode();
  return focusedNode ? focusedNode->OwnerDoc() : nullptr;
}

}  // namespace a11y
}  // namespace mozilla
