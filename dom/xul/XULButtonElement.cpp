/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "XULButtonElement.h"

#include "XULMenuParentElement.h"
#include "XULPopupElement.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/XULMenuBarElement.h"
#include "mozilla/glue/Debug.h"
#include "nsCaseTreatment.h"
#include "nsChangeHint.h"
#include "nsGkAtoms.h"
#include "nsIDOMXULButtonElement.h"
#include "nsISound.h"
#include "nsITimer.h"
#include "nsLayoutUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"
#include "nsXULPopupManager.h"

namespace mozilla::dom {

XULButtonElement::XULButtonElement(
    already_AddRefed<mozilla::dom::NodeInfo> aNodeInfo)
    : nsXULElement(std::move(aNodeInfo)),
      mIsAlwaysMenu(IsAnyOfXULElements(nsGkAtoms::menu, nsGkAtoms::menulist,
                                       nsGkAtoms::menuitem)),
      mCheckable(IsAnyOfXULElements(nsGkAtoms::menuitem,
                                    nsGkAtoms::richlistitem, nsGkAtoms::radio,
                                    nsGkAtoms::checkbox)) {}

XULButtonElement::~XULButtonElement() {
  StopBlinking();
  KillMenuOpenTimer();
}

nsChangeHint XULButtonElement::GetAttributeChangeHint(
    const nsAtom* aAttribute, AttrModType aModType) const {
  if (aAttribute == nsGkAtoms::type &&
      IsAnyOfXULElements(nsGkAtoms::button, nsGkAtoms::toolbarbutton)) {
    // type=menu switches to a menu frame.
    return nsChangeHint_ReconstructFrame;
  }
  return nsXULElement::GetAttributeChangeHint(aAttribute, aModType);
}

// This global flag is used to record the timestamp when a menu was opened or
// closed and is used to ignore the mousemove and mouseup events that would fire
// on the menu after the mousedown occurred.
static TimeStamp gMenuJustOpenedOrClosedTime = TimeStamp();

void XULButtonElement::PopupOpened() {
  if (!IsMenu()) {
    return;
  }
  gMenuJustOpenedOrClosedTime = TimeStamp::Now();
  SetAttr(kNameSpaceID_None, nsGkAtoms::open, u"true"_ns, true,
          IsKnownNewAttr::No);
}

void XULButtonElement::PopupClosed(bool aDeselectMenu) {
  if (!IsMenu()) {
    return;
  }
  nsContentUtils::AddScriptRunner(
      MakeAndAddRef<nsUnsetAttrRunnable>(this, nsGkAtoms::open));

  if (aDeselectMenu) {
    if (RefPtr<XULMenuParentElement> parent = GetMenuParent()) {
      if (parent->GetActiveMenuChild() == this) {
        parent->SetActiveMenuChild(nullptr);
      }
    }
  }
}

bool XULButtonElement::IsMenuActive() const {
  if (XULMenuParentElement* menu = GetMenuParent()) {
    return menu->GetActiveMenuChild() == this;
  }
  return false;
}

void XULButtonElement::HandleEnterKeyPress(WidgetEvent& aEvent) {
  if (IsDisabled()) {
#ifdef XP_WIN
    if (XULPopupElement* popup = GetContainingPopupElement()) {
      if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
        pm->HidePopup(
            popup, {HidePopupOption::HideChain, HidePopupOption::DeselectMenu,
                    HidePopupOption::Async});
      }
    }
#endif
    return;
  }
  if (IsMenuPopupOpen()) {
    return;
  }
  // The enter key press applies to us.
  if (IsMenuItem()) {
    ExecuteMenu(aEvent);
  } else {
    OpenMenuPopup(true);
  }
}

bool XULButtonElement::IsMenuPopupOpen() {
  nsMenuPopupFrame* popupFrame = GetMenuPopup(FlushType::None);
  return popupFrame && popupFrame->IsOpen();
}

bool XULButtonElement::IsOnMenu() const {
  auto* popup = XULPopupElement::FromNodeOrNull(GetMenuParent());
  return popup && popup->IsMenu();
}

bool XULButtonElement::IsOnMenuList() const {
  if (XULMenuParentElement* menu = GetMenuParent()) {
    return menu->GetParent() &&
           menu->GetParent()->IsXULElement(nsGkAtoms::menulist);
  }
  return false;
}

bool XULButtonElement::IsOnMenuBar() const {
  if (XULMenuParentElement* menu = GetMenuParent()) {
    return menu->IsMenuBar();
  }
  return false;
}

nsMenuPopupFrame* XULButtonElement::GetContainingPopupWithoutFlushing() const {
  if (XULPopupElement* popup = GetContainingPopupElement()) {
    return do_QueryFrame(popup->GetPrimaryFrame());
  }
  return nullptr;
}

XULPopupElement* XULButtonElement::GetContainingPopupElement() const {
  return XULPopupElement::FromNodeOrNull(GetMenuParent());
}

bool XULButtonElement::IsOnContextMenu() const {
  if (nsMenuPopupFrame* popup = GetContainingPopupWithoutFlushing()) {
    return popup->IsContextMenu();
  }
  return false;
}

void XULButtonElement::ToggleMenuState() {
  if (IsMenuPopupOpen()) {
    CloseMenuPopup(false);
  } else {
    OpenMenuPopup(false);
  }
}

void XULButtonElement::KillMenuOpenTimer() {
  if (mMenuOpenTimer) {
    mMenuOpenTimer->Cancel();
    mMenuOpenTimer = nullptr;
  }
}

void XULButtonElement::OpenMenuPopup(bool aSelectFirstItem) {
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return;
  }

  pm->KillMenuTimer();
  if (!pm->MayShowMenu(this)) {
    return;
  }

  if (RefPtr<XULMenuParentElement> parent = GetMenuParent()) {
    parent->SetActiveMenuChild(this);
  }

  // Open the menu asynchronously.
  OwnerDoc()->Dispatch(NS_NewRunnableFunction(
      "AsyncOpenMenu", [self = RefPtr{this}, aSelectFirstItem] {
        if (self->GetMenuParent() && !self->IsMenuActive()) {
          return;
        }
        if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
          pm->ShowMenu(self, aSelectFirstItem);
        }
      }));
}

void XULButtonElement::CloseMenuPopup(bool aDeselectMenu) {
  gMenuJustOpenedOrClosedTime = TimeStamp::Now();
  // Close the menu asynchronously
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return;
  }
  if (auto* popup = GetMenuPopupContent()) {
    HidePopupOptions options{HidePopupOption::Async};
    if (aDeselectMenu) {
      options += HidePopupOption::DeselectMenu;
    }
    pm->HidePopup(popup, options);
  }
}

int32_t XULButtonElement::MenuOpenCloseDelay() const {
  if (IsOnMenuBar()) {
    return 0;
  }
  return LookAndFeel::GetInt(LookAndFeel::IntID::SubmenuDelay, 300);  // ms
}

void XULButtonElement::ExecuteMenu(Modifiers aModifiers, int16_t aButton,
                                   bool aIsTrusted) {
  MOZ_ASSERT(IsMenu());

  StopBlinking();

  auto menuType = GetMenuType();
  if (NS_WARN_IF(!menuType)) {
    return;
  }

  // Because the command event is firing asynchronously, a flag is needed to
  // indicate whether user input is being handled. This ensures that a popup
  // window won't get blocked.
  const bool userinput = dom::UserActivation::IsHandlingUserInput();

  // Flip "checked" state if we're a checkbox menu, or an un-checked radio menu.
  bool needToFlipChecked = false;
  if (*menuType == MenuType::Checkbox ||
      (*menuType == MenuType::Radio && !GetBoolAttr(nsGkAtoms::checked))) {
    needToFlipChecked = !AttrValueIs(kNameSpaceID_None, nsGkAtoms::autocheck,
                                     nsGkAtoms::_false, eCaseMatters);
  }

  mDelayedMenuCommandEvent = new nsXULMenuCommandEvent(
      this, aIsTrusted, aModifiers, userinput, needToFlipChecked, aButton);
  StartBlinking();
}

void XULButtonElement::StopBlinking() {
  if (mMenuBlinkTimer) {
    if (auto* parent = GetMenuParent()) {
      parent->LockMenuUntilClosed(false);
    }
    mMenuBlinkTimer->Cancel();
    mMenuBlinkTimer = nullptr;
  }
  mDelayedMenuCommandEvent = nullptr;
}

void XULButtonElement::PassMenuCommandEventToPopupManager() {
  if (mDelayedMenuCommandEvent) {
    if (RefPtr<nsXULPopupManager> pm = nsXULPopupManager::GetInstance()) {
      RefPtr<nsXULMenuCommandEvent> event = std::move(mDelayedMenuCommandEvent);
      nsCOMPtr<nsIContent> content = this;
      pm->ExecuteMenu(content, event);
    }
  }
  mDelayedMenuCommandEvent = nullptr;
}

static constexpr int32_t kBlinkDelay = 67;  // milliseconds

void XULButtonElement::StartBlinking() {
  if (!LookAndFeel::GetInt(LookAndFeel::IntID::ChosenMenuItemsShouldBlink)) {
    PassMenuCommandEventToPopupManager();
    return;
  }

  // Blink off.
  UnsetAttr(kNameSpaceID_None, nsGkAtoms::menuactive, true);
  if (auto* parent = GetMenuParent()) {
    // Make this menu ignore events from now on.
    parent->LockMenuUntilClosed(true);
  }

  // Set up a timer to blink back on.
  NS_NewTimerWithFuncCallback(
      getter_AddRefs(mMenuBlinkTimer),
      [](nsITimer*, void* aClosure) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
        RefPtr self = static_cast<XULButtonElement*>(aClosure);
        if (auto* parent = self->GetMenuParent()) {
          if (parent->GetActiveMenuChild() == self) {
            // Restore the highlighting if we're still the active item.
            self->SetAttr(kNameSpaceID_None, nsGkAtoms::menuactive, u"true"_ns,
                          true, IsKnownNewAttr::No);
          }
        }
        // Reuse our timer to actually execute.
        self->mMenuBlinkTimer->InitWithNamedFuncCallback(
            [](nsITimer*, void* aClosure) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
              RefPtr self = static_cast<XULButtonElement*>(aClosure);
              if (auto* parent = self->GetMenuParent()) {
                parent->LockMenuUntilClosed(false);
              }
              self->PassMenuCommandEventToPopupManager();
              self->StopBlinking();
            },
            aClosure, kBlinkDelay, nsITimer::TYPE_ONE_SHOT,
            "XULButtonElement::ContinueBlinking"_ns);
      },
      this, kBlinkDelay, nsITimer::TYPE_ONE_SHOT,
      "XULButtonElement::StartBlinking"_ns, GetMainThreadSerialEventTarget());
}

void XULButtonElement::UnbindFromTree(UnbindContext& aContext) {
  StopBlinking();
  nsXULElement::UnbindFromTree(aContext);
}

void XULButtonElement::ExecuteMenu(WidgetEvent& aEvent) {
  MOZ_ASSERT(IsMenu());
  if (nsCOMPtr<nsISound> sound = do_GetService("@mozilla.org/sound;1")) {
    sound->PlayEventSound(nsISound::EVENT_MENU_EXECUTE);
  }

  Modifiers modifiers = 0;
  if (WidgetInputEvent* inputEvent = aEvent.AsInputEvent()) {
    modifiers = inputEvent->mModifiers;
  }

  int16_t button = 0;
  if (WidgetMouseEventBase* mouseEvent = aEvent.AsMouseEventBase()) {
    button = mouseEvent->mButton;
  }

  ExecuteMenu(modifiers, button, aEvent.IsTrusted());
}

void XULButtonElement::PostHandleEventForMenus(
    EventChainPostVisitor& aVisitor) {
  auto* event = aVisitor.mEvent;

  if (event->mOriginalTarget != this) {
    return;
  }

  if (auto* parent = GetMenuParent()) {
    if (NS_WARN_IF(parent->IsLocked())) {
      return;
    }
  }

  // If a menu just opened, ignore the mouseup event that might occur after a
  // the mousedown event that opened it. However, if a different mousedown event
  // occurs, just clear this flag.
  if (!gMenuJustOpenedOrClosedTime.IsNull()) {
    if (event->mMessage == eMouseDown) {
      gMenuJustOpenedOrClosedTime = TimeStamp();
    } else if (event->mMessage == eMouseUp) {
      return;
    }
  }

  if (event->mMessage == eKeyPress && !IsDisabled()) {
    WidgetKeyboardEvent* keyEvent = event->AsKeyboardEvent();
    uint32_t keyCode = keyEvent->mKeyCode;
#ifdef XP_MACOSX
    // On mac, open menulist on either up/down arrow or space (w/o Cmd pressed)
    if (!IsMenuPopupOpen() &&
        ((keyEvent->mCharCode == ' ' && !keyEvent->IsMeta()) ||
         (keyCode == NS_VK_UP || keyCode == NS_VK_DOWN))) {
      // When pressing space, don't open the menu if performing an incremental
      // search.
      if (keyEvent->mCharCode != ' ' ||
          !nsMenuPopupFrame::IsWithinIncrementalTime(keyEvent->mTimeStamp)) {
        aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        OpenMenuPopup(false);
      }
    }
#else
    // On other platforms, toggle menulist on unmodified F4 or Alt arrow
    if ((keyCode == NS_VK_F4 && !keyEvent->IsAlt()) ||
        ((keyCode == NS_VK_UP || keyCode == NS_VK_DOWN) && keyEvent->IsAlt())) {
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      ToggleMenuState();
    }
#endif
  } else if (event->mMessage == eMouseDown &&
             event->AsMouseEvent()->mButton == MouseButton::ePrimary &&
#ifdef XP_MACOSX
             // On mac, ctrl-click will send a context menu event from the
             // widget, so we don't want to bring up the menu.
             !event->AsMouseEvent()->IsControl() &&
#endif
             !IsDisabled() && !IsMenuItem()) {
    // The menu item was selected. Bring up the menu.
    // We have children.
    // Don't prevent the default action here, since that will also cancel
    // potential drag starts.
    if (!IsOnMenu()) {
      ToggleMenuState();
    } else if (!IsMenuPopupOpen()) {
      OpenMenuPopup(false);
    }
  } else if (event->mMessage == eMouseUp && IsMenuItem() && !IsDisabled() &&
             !event->mFlags.mMultipleActionsPrevented) {
    // We accept left and middle clicks on all menu items to activate the item.
    // On context menus we also accept right click to activate the item, because
    // right-clicking on an item in a context menu cannot open another context
    // menu.
    bool isMacCtrlClick = false;
#ifdef XP_MACOSX
    isMacCtrlClick = event->AsMouseEvent()->mButton == MouseButton::ePrimary &&
                     event->AsMouseEvent()->IsControl();
#endif
    bool clickMightOpenContextMenu =
        event->AsMouseEvent()->mButton == MouseButton::eSecondary ||
        isMacCtrlClick;
    if (!clickMightOpenContextMenu || IsOnContextMenu()) {
      aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      ExecuteMenu(*event);
    }
  } else if (event->mMessage == eContextMenu && IsOnContextMenu() &&
             !IsMenuItem() && !IsDisabled()) {
    // Make sure we cancel default processing of the context menu event so
    // that it doesn't bubble and get seen again by the popuplistener and show
    // another context menu.
    aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
  } else if (event->mMessage == eMouseOut) {
    KillMenuOpenTimer();
    if (RefPtr<XULMenuParentElement> parent = GetMenuParent()) {
      if (parent->GetActiveMenuChild() == this) {
        // Deactivate the menu on mouse out in some cases...
        const bool shouldDeactivate = [&] {
          if (IsMenuPopupOpen()) {
            // If we're open we never deselect. PopupClosed will do as needed.
            return false;
          }
          if (auto* menubar = XULMenuBarElement::FromNode(*parent)) {
            // De-select when exiting a menubar item, if the menubar wasn't
            // activated by keyboard.
            return !menubar->IsActiveByKeyboard();
          }
          if (IsOnMenuList()) {
            // Don't de-select if on a menu-list. That matches Chromium and our
            // historical Windows behavior, see bug 1197913.
            return false;
          }
          // De-select elsewhere.
          return true;
        }();

        if (shouldDeactivate) {
          parent->SetActiveMenuChild(nullptr);
        }
      }
    }
  } else if (event->mMessage == eMouseMove && (IsOnMenu() || IsOnMenuBar())) {
    // Use a tolerance to address situations where a user might perform a
    // "wiggly" click that is accompanied by near-simultaneous mousemove events.
    const TimeDuration kTolerance = TimeDuration::FromMilliseconds(200);
    if (!gMenuJustOpenedOrClosedTime.IsNull() &&
        gMenuJustOpenedOrClosedTime + kTolerance < TimeStamp::Now()) {
      gMenuJustOpenedOrClosedTime = TimeStamp();
      return;
    }

    if (IsDisabled() && IsOnMenuList()) {
      return;
    }

    RefPtr<XULMenuParentElement> parent = GetMenuParent();
    MOZ_ASSERT(parent, "How did IsOnMenu{,Bar} return true then?");

    const bool isOnOpenMenubar =
        parent->IsMenuBar() && parent->GetActiveMenuChild() &&
        parent->GetActiveMenuChild()->IsMenuPopupOpen();

    parent->SetActiveMenuChild(this);

    // We need to check if we really became the current menu item or not.
    if (!IsMenuActive()) {
      // We didn't (presumably because a context menu was active)
      return;
    }
    if (IsDisabled() || IsMenuItem() || IsMenuPopupOpen() || mMenuOpenTimer) {
      // Disabled, or already opening or what not.
      return;
    }

    if (parent->IsMenuBar() && !isOnOpenMenubar) {
      // We should only open on hover in the menubar iff the menubar is open
      // already.
      return;
    }

    // A timer is used so that it doesn't open if the user moves the mouse
    // quickly past the menu. The MenuOpenCloseDelay ensures that only menus
    // have this behaviour.
    NS_NewTimerWithFuncCallback(
        getter_AddRefs(mMenuOpenTimer),
        [](nsITimer*, void* aClosure) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
          RefPtr self = static_cast<XULButtonElement*>(aClosure);
          self->mMenuOpenTimer = nullptr;
          if (self->IsMenuPopupOpen()) {
            return;
          }
          // make sure we didn't open a context menu in the meantime
          // (i.e. the user right-clicked while hovering over a submenu).
          nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
          if (!pm) {
            return;
          }
          if (pm->HasContextMenu(nullptr) && !self->IsOnContextMenu()) {
            return;
          }
          if (!self->IsMenuActive()) {
            return;
          }
          self->OpenMenuPopup(false);
        },
        this, MenuOpenCloseDelay(), nsITimer::TYPE_ONE_SHOT,
        "XULButtonElement::OpenMenu"_ns, GetMainThreadSerialEventTarget());
  }
}

void XULButtonElement::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mWantsPostHandleEvent = true;
  nsXULElement::GetEventTargetParent(aVisitor);
}

nsresult XULButtonElement::PostHandleEvent(EventChainPostVisitor& aVisitor) {
  if (aVisitor.mEventStatus == nsEventStatus_eConsumeNoDefault) {
    return NS_OK;
  }

  if (IsMenu()) {
    PostHandleEventForMenus(aVisitor);
    return NS_OK;
  }

  auto* event = aVisitor.mEvent;
  switch (event->mMessage) {
    case eBlur: {
      Blurred();
      break;
    }
    case eKeyDown: {
      WidgetKeyboardEvent* keyEvent = event->AsKeyboardEvent();
      if (!keyEvent) {
        break;
      }
      if (keyEvent->ShouldWorkAsSpaceKey() && aVisitor.mPresContext &&
          !IsDisabled()) {
        EventStateManager* esm = aVisitor.mPresContext->EventStateManager();
        // :hover:active state
        esm->SetContentState(this, ElementState::HOVER);
        esm->SetContentState(this, ElementState::ACTIVE);
        mIsHandlingKeyEvent = true;
      }
      break;
    }

    case eKeyPress: {
      WidgetKeyboardEvent* keyEvent = event->AsKeyboardEvent();
      if (!keyEvent) {
        break;
      }
// On mac, Return fires the default button, not the focused one.
#ifndef XP_MACOSX
      if (NS_VK_RETURN == keyEvent->mKeyCode) {
        if (RefPtr<nsIDOMXULButtonElement> button = AsXULButton()) {
          if (OnPointerClicked(*keyEvent)) {
            aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
          }
        }
      }
#endif
      if (keyEvent->ShouldWorkAsSpaceKey() && mIsHandlingKeyEvent) {
        // Prevent scrolling.
        aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
      }
      break;
    }

    case eKeyUp: {
      WidgetKeyboardEvent* keyEvent = event->AsKeyboardEvent();
      if (!keyEvent) {
        break;
      }
      if (keyEvent->ShouldWorkAsSpaceKey()) {
        mIsHandlingKeyEvent = false;
        if (State().HasAllStates(ElementState::ACTIVE | ElementState::HOVER) &&
            aVisitor.mPresContext) {
          // return to normal state
          EventStateManager* esm = aVisitor.mPresContext->EventStateManager();
          esm->SetContentState(nullptr, ElementState::ACTIVE);
          esm->SetContentState(nullptr, ElementState::HOVER);
          if (OnPointerClicked(*keyEvent)) {
            aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
          }
        }
      }
      break;
    }

    case ePointerClick: {
      WidgetMouseEvent* mouseEvent = event->AsMouseEvent();
      if (mouseEvent->IsLeftClickEvent()) {
        if (OnPointerClicked(*mouseEvent)) {
          aVisitor.mEventStatus = nsEventStatus_eConsumeNoDefault;
        }
      }
      break;
    }

    default:
      break;
  }

  return NS_OK;
}

void XULButtonElement::Blurred() {
  if (mIsHandlingKeyEvent &&
      State().HasAllStates(ElementState::ACTIVE | ElementState::HOVER)) {
    // Return to normal state
    if (nsPresContext* pc = OwnerDoc()->GetPresContext()) {
      EventStateManager* esm = pc->EventStateManager();
      esm->SetContentState(nullptr, ElementState::ACTIVE);
      esm->SetContentState(nullptr, ElementState::HOVER);
    }
  }
  mIsHandlingKeyEvent = false;
}

bool XULButtonElement::OnPointerClicked(WidgetGUIEvent& aEvent) {
  // Don't execute if we're disabled.
  if (IsDisabled() || !IsInComposedDoc()) {
    return false;
  }

  if (NodeInfo()->Equals(nsGkAtoms::checkbox)) {
    // Toggle `checked` now so that command handling sees the right state.
    SetBoolAttr(nsGkAtoms::checked, !GetBoolAttr(nsGkAtoms::checked));
  }

  // Have the content handle the event, propagating it according to normal DOM
  // rules.
  RefPtr<mozilla::PresShell> presShell = OwnerDoc()->GetPresShell();
  if (!presShell) {
    return false;
  }

  // Execute the oncommand event handler.
  WidgetInputEvent* inputEvent = aEvent.AsInputEvent();
  WidgetMouseEventBase* mouseEvent = aEvent.AsMouseEventBase();
  WidgetKeyboardEvent* keyEvent = aEvent.AsKeyboardEvent();
  // TODO: Set aSourceEvent?
  nsContentUtils::DispatchXULCommand(
      this, aEvent.IsTrusted(), /* aSourceEvent = */ nullptr, presShell,
      inputEvent->IsControl(), inputEvent->IsAlt(), inputEvent->IsShift(),
      inputEvent->IsMeta(),
      mouseEvent ? mouseEvent->mInputSource
                 : (keyEvent ? MouseEvent_Binding::MOZ_SOURCE_KEYBOARD
                             : MouseEvent_Binding::MOZ_SOURCE_UNKNOWN),
      mouseEvent ? mouseEvent->mButton : 0);
  return true;
}

bool XULButtonElement::IsMenu() const {
  if (mIsAlwaysMenu) {
    return true;
  }
  return IsAnyOfXULElements(nsGkAtoms::button, nsGkAtoms::toolbarbutton) &&
         AttrValueIs(kNameSpaceID_None, nsGkAtoms::type, nsGkAtoms::menu,
                     eCaseMatters);
}

void XULButtonElement::UncheckRadioSiblings() {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(GetMenuType() == Some(MenuType::Radio));
  nsAutoString groupName;
  GetAttr(nsGkAtoms::name, groupName);

  nsIContent* parent = GetParent();
  if (!parent) {
    return;
  }

  auto ShouldUncheck = [&](const nsIContent& aSibling) {
    const auto* button = XULButtonElement::FromNode(aSibling);
    if (!button || button->GetMenuType() != Some(MenuType::Radio)) {
      return false;
    }
    if (const auto* attr = button->GetParsedAttr(nsGkAtoms::name)) {
      if (!attr->Equals(groupName, eCaseMatters)) {
        return false;
      }
    } else if (!groupName.IsEmpty()) {
      return false;
    }
    // we're in the same group, only uncheck if we're checked (for some reason,
    // some tests rely on that specifically).
    return button->GetBoolAttr(nsGkAtoms::checked);
  };

  for (nsIContent* child = parent->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child == this || !ShouldUncheck(*child)) {
      continue;
    }
    child->AsElement()->UnsetAttr(nsGkAtoms::checked, IgnoreErrors());
  }
}

nsAtom* XULButtonElement::GetCheckedStateAttribute() const {
  // <menuitem> uses checked for type=radio / type=checkbox, and selected for
  // <menulist>.
  //
  // <richlistitem> uses checked, and <radio> uses selected. Note that
  // <richlistitem> uses selected with a different meaning as well (for the
  // currently selected / focused item), so this code needs to deal with that.
  MOZ_ASSERT(mCheckable);
  if (auto menuType = GetMenuType()) {
    return *menuType == MenuType::Normal ? nsGkAtoms::selected
                                         : nsGkAtoms::checked;
  }
  if (NodeInfo()->Equals(nsGkAtoms::radio)) {
    return nsGkAtoms::selected;
  }
  return nsGkAtoms::checked;
}

void XULButtonElement::AfterSetAttr(int32_t aNamespaceID, nsAtom* aName,
                                    const nsAttrValue* aValue,
                                    const nsAttrValue* aOldValue,
                                    nsIPrincipal* aSubjectPrincipal,
                                    bool aNotify) {
  nsXULElement::AfterSetAttr(aNamespaceID, aName, aValue, aOldValue,
                             aSubjectPrincipal, aNotify);
  if (aNamespaceID != kNameSpaceID_None) {
    return;
  }
  if (mCheckable) {
    if (aName == GetCheckedStateAttribute()) {
      SetStates(ElementState::CHECKED, !!aValue, aNotify);
    }
    if (IsAlwaysMenu() && aName == nsGkAtoms::type) {
      SetStates(ElementState::CHECKED, GetBoolAttr(GetCheckedStateAttribute()),
                aNotify);
    }
  }
  if (aName == nsGkAtoms::disabled) {
    SetStates(ElementState::DISABLED, !!aValue, aNotify);
  }
  if (IsAlwaysMenu()) {
    // We need to uncheck radio siblings when we're a checked radio and switch
    // groups, or become checked.
    const bool shouldUncheckSiblings = [&] {
      if (aName == nsGkAtoms::type || aName == nsGkAtoms::name) {
        return *GetMenuType() == MenuType::Radio &&
               GetBoolAttr(nsGkAtoms::checked);
      }
      if (aName == nsGkAtoms::checked && aValue) {
        return *GetMenuType() == MenuType::Radio;
      }
      return false;
    }();
    if (shouldUncheckSiblings) {
      UncheckRadioSiblings();
    }
  }
}

auto XULButtonElement::GetMenuType() const -> Maybe<MenuType> {
  if (!IsAlwaysMenu()) {
    return Nothing();
  }

  static Element::AttrValuesArray values[] = {nsGkAtoms::checkbox,
                                              nsGkAtoms::radio, nullptr};
  switch (FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::type, values,
                          eCaseMatters)) {
    case 0:
      return Some(MenuType::Checkbox);
    case 1:
      return Some(MenuType::Radio);
    default:
      return Some(MenuType::Normal);
  }
}

XULMenuBarElement* XULButtonElement::GetMenuBar() const {
  if (!IsMenu()) {
    return nullptr;
  }
  return FirstAncestorOfType<XULMenuBarElement>();
}

XULMenuParentElement* XULButtonElement::GetMenuParent() const {
  if (IsXULElement(nsGkAtoms::menulist)) {
    return nullptr;
  }
  return FirstAncestorOfType<XULMenuParentElement>();
}

XULPopupElement* XULButtonElement::GetMenuPopupContent() const {
  if (!IsMenu()) {
    return nullptr;
  }
  for (auto* child = GetFirstChild(); child; child = child->GetNextSibling()) {
    if (auto* popup = XULPopupElement::FromNode(child)) {
      return popup;
    }
  }
  return nullptr;
}

nsMenuPopupFrame* XULButtonElement::GetMenuPopupWithoutFlushing() const {
  return const_cast<XULButtonElement*>(this)->GetMenuPopup(FlushType::None);
}

nsMenuPopupFrame* XULButtonElement::GetMenuPopup(FlushType aFlushType) {
  RefPtr popup = GetMenuPopupContent();
  if (!popup) {
    return nullptr;
  }
  return do_QueryFrame(popup->GetPrimaryFrame(aFlushType));
}

bool XULButtonElement::OpenedWithKey() const {
  auto* menubar = GetMenuBar();
  return menubar && menubar->IsActiveByKeyboard();
}

}  // namespace mozilla::dom
