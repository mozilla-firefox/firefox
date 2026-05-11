/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXULPopupManager.h"

#include "WindowRenderer.h"
#include "XULButtonElement.h"
#include "mozilla/AnimationUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/FlushType.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PointerLockManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"  // for Event
#include "mozilla/dom/HTMLSlotElement.h"
#include "mozilla/dom/KeyboardEvent.h"
#include "mozilla/dom/KeyboardEventBinding.h"
#include "mozilla/dom/MouseEvent.h"
#include "mozilla/dom/PopupPositionedEvent.h"
#include "mozilla/dom/PopupPositionedEventBinding.h"
#include "mozilla/dom/UIEvent.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/XULCommandEvent.h"
#include "mozilla/dom/XULMenuBarElement.h"
#include "mozilla/dom/XULMenuElement.h"
#include "mozilla/dom/XULPopupElement.h"
#include "mozilla/widget/NativeMenuSupport.h"
#include "mozilla/widget/nsAutoRollup.h"
#include "nsCSSFrameConstructor.h"
#include "nsCaret.h"
#include "nsContentUtils.h"
#include "nsFocusManager.h"
#include "nsFrameManager.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowOuter.h"
#include "nsIBaseWindow.h"
#include "nsIContentInlines.h"
#include "nsIDOMXULCommandDispatcher.h"
#include "nsIDocShell.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIObserverService.h"
#include "nsISound.h"
#include "nsITimer.h"
#include "nsLayoutUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsPIDOMWindow.h"
#include "nsPIWindowRoot.h"
#include "nsPresContextInlines.h"
#include "nsXULElement.h"

using namespace mozilla;
using namespace mozilla::dom;
using mozilla::widget::NativeMenu;

static_assert(KeyboardEvent_Binding::DOM_VK_HOME ==
                      KeyboardEvent_Binding::DOM_VK_END + 1 &&
                  KeyboardEvent_Binding::DOM_VK_LEFT ==
                      KeyboardEvent_Binding::DOM_VK_END + 2 &&
                  KeyboardEvent_Binding::DOM_VK_UP ==
                      KeyboardEvent_Binding::DOM_VK_END + 3 &&
                  KeyboardEvent_Binding::DOM_VK_RIGHT ==
                      KeyboardEvent_Binding::DOM_VK_END + 4 &&
                  KeyboardEvent_Binding::DOM_VK_DOWN ==
                      KeyboardEvent_Binding::DOM_VK_END + 5,
              "nsXULPopupManager assumes some keyCode values are consecutive");

#define NS_DIRECTION_IS_INLINE(dir) \
  (dir == eNavigationDirection_Start || dir == eNavigationDirection_End)
#define NS_DIRECTION_IS_BLOCK(dir) \
  (dir == eNavigationDirection_Before || dir == eNavigationDirection_After)
#define NS_DIRECTION_IS_BLOCK_TO_EDGE(dir) \
  (dir == eNavigationDirection_First || dir == eNavigationDirection_Last)

static_assert(static_cast<uint8_t>(mozilla::StyleDirection::Ltr) == 0 &&
                  static_cast<uint8_t>(mozilla::StyleDirection::Rtl) == 1,
              "Left to Right should be 0 and Right to Left should be 1");

const nsNavigationDirection DirectionFromKeyCodeTable[2][6] = {
    {
        eNavigationDirection_Last,    // KeyboardEvent_Binding::DOM_VK_END
        eNavigationDirection_First,   // KeyboardEvent_Binding::DOM_VK_HOME
        eNavigationDirection_Start,   // KeyboardEvent_Binding::DOM_VK_LEFT
        eNavigationDirection_Before,  // KeyboardEvent_Binding::DOM_VK_UP
        eNavigationDirection_End,     // KeyboardEvent_Binding::DOM_VK_RIGHT
        eNavigationDirection_After    // KeyboardEvent_Binding::DOM_VK_DOWN
    },
    {
        eNavigationDirection_Last,    // KeyboardEvent_Binding::DOM_VK_END
        eNavigationDirection_First,   // KeyboardEvent_Binding::DOM_VK_HOME
        eNavigationDirection_End,     // KeyboardEvent_Binding::DOM_VK_LEFT
        eNavigationDirection_Before,  // KeyboardEvent_Binding::DOM_VK_UP
        eNavigationDirection_Start,   // KeyboardEvent_Binding::DOM_VK_RIGHT
        eNavigationDirection_After    // KeyboardEvent_Binding::DOM_VK_DOWN
    }};

StaticRefPtr<nsXULPopupManager> nsXULPopupManager::sInstance;

PendingPopup::PendingPopup(Element* aPopup, mozilla::dom::Event* aEvent)
    : mPopup(aPopup), mEvent(aEvent), mModifiers(0) {
  InitMousePoint();
}

void PendingPopup::InitMousePoint() {
  // get the event coordinates relative to the root frame of the document
  // containing the popup.
  if (!mEvent) {
    return;
  }

  WidgetEvent* event = mEvent->WidgetEventPtr();
  WidgetInputEvent* inputEvent = event->AsInputEvent();
  if (inputEvent) {
    mModifiers = inputEvent->mModifiers;
  }
  Document* doc = mPopup->GetUncomposedDoc();
  if (!doc) {
    return;
  }

  PresShell* presShell = doc->GetPresShell();
  nsPresContext* presContext;
  if (presShell && (presContext = presShell->GetPresContext())) {
    nsPresContext* rootDocPresContext = presContext->GetRootPresContext();
    if (!rootDocPresContext) {
      return;
    }

    nsIFrame* rootDocumentRootFrame =
        rootDocPresContext->PresShell()->GetRootFrame();
    if ((event->IsMouseEventClassOrHasClickRelatedPointerEvent() ||
         event->mClass == eMouseScrollEventClass ||
         event->mClass == eWheelEventClass) &&
        !event->AsGUIEvent()->mWidget) {
      // no widget, so just use the client point if available
      MouseEvent* mouseEvent = mEvent->AsMouseEvent();
      const CSSIntPoint clientPt(RoundedToInt(mouseEvent->ClientPoint()));

      // XXX this doesn't handle IFRAMEs in transforms
      nsPoint thisDocToRootDocOffset =
          presShell->GetRootFrame()->GetOffsetToCrossDoc(rootDocumentRootFrame);
      // convert to device pixels
      mMousePoint.x = presContext->AppUnitsToDevPixels(
          nsPresContext::CSSPixelsToAppUnits(clientPt.x) +
          thisDocToRootDocOffset.x);
      mMousePoint.y = presContext->AppUnitsToDevPixels(
          nsPresContext::CSSPixelsToAppUnits(clientPt.y) +
          thisDocToRootDocOffset.y);
    } else if (rootDocumentRootFrame) {
      nsPoint pnt = nsLayoutUtils::GetEventCoordinatesRelativeTo(
          event, RelativeTo{rootDocumentRootFrame});
      mMousePoint =
          LayoutDeviceIntPoint(rootDocPresContext->AppUnitsToDevPixels(pnt.x),
                               rootDocPresContext->AppUnitsToDevPixels(pnt.y));
    }
  }
}

already_AddRefed<nsIContent> PendingPopup::GetTriggerContent() const {
  nsCOMPtr<nsIContent> target =
      do_QueryInterface(mEvent ? mEvent->GetTarget() : nullptr);
  return target.forget();
}

uint16_t PendingPopup::MouseInputSource() const {
  if (mEvent) {
    mozilla::WidgetMouseEventBase* mouseEvent =
        mEvent->WidgetEventPtr()->AsMouseEventBase();
    if (mouseEvent) {
      return mouseEvent->mInputSource;
    }

    RefPtr<XULCommandEvent> commandEvent = mEvent->AsXULCommandEvent();
    if (commandEvent) {
      return commandEvent->InputSource();
    }
  }

  return MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;
}

XULPopupElement* nsMenuChainItem::Element() { return &mFrame->PopupElement(); }

void nsMenuChainItem::SetParent(UniquePtr<nsMenuChainItem> aParent) {
  MOZ_ASSERT_IF(aParent, !aParent->mChild);
  auto oldParent = Detach();
  mParent = std::move(aParent);
  if (mParent) {
    mParent->mChild = this;
  }
}

UniquePtr<nsMenuChainItem> nsMenuChainItem::Detach() {
  if (mParent) {
    MOZ_ASSERT(mParent->mChild == this,
               "Unexpected - parent's child not set to this");
    mParent->mChild = nullptr;
  }
  return std::move(mParent);
}

void nsXULPopupManager::AddMenuChainItem(UniquePtr<nsMenuChainItem> aItem) {
  auto* frame = aItem->Frame();
  PopupType popupType = frame->GetPopupType();
  if (StaticPrefs::layout_cursor_disable_for_popups() &&
      popupType != PopupType::Tooltip) {
    if (auto* rootPc = frame->PresContext()->GetRootPresContext()) {
      if (nsCOMPtr<nsIWidget> rootWidget = rootPc->GetRootWidget()) {
        rootWidget->SetCustomCursorAllowed(false);
      }
    }
  }

  // popups normally hide when an outside click occurs. Panels may use
  // the noautohide attribute to disable this behaviour. It is expected
  // that the application will hide these popups manually. The tooltip
  // listener will handle closing the tooltip also.
  nsIContent* oldmenu = nullptr;
  if (mPopups) {
    oldmenu = mPopups->Element();
  }
  aItem->SetParent(std::move(mPopups));
  mPopups = std::move(aItem);
  SetCaptureState(oldmenu);
}

void nsXULPopupManager::RemoveMenuChainItem(nsMenuChainItem* aItem) {
  nsPresContext* rootPC = aItem->Frame()->PresContext()->GetRootPresContext();
  auto matcher = [&](nsMenuChainItem* aChainItem) -> bool {
    return aChainItem != aItem &&
           rootPC == aChainItem->Frame()->PresContext()->GetRootPresContext();
  };
  if (rootPC && !FirstMatchingPopup(matcher)) {
    if (nsCOMPtr<nsIWidget> rootWidget = rootPC->GetRootWidget()) {
      rootWidget->SetCustomCursorAllowed(true);
    }
  }

  auto parent = aItem->Detach();
  if (auto* child = aItem->GetChild()) {
    MOZ_ASSERT(aItem != mPopups,
               "Unexpected - popup with child at end of chain");
    // This will kill aItem by changing child's mParent pointer.
    child->SetParent(std::move(parent));
  } else {
    // An item without a child should be the first item in the chain, so set
    // the first item pointer, pointed to by aRoot, to the parent.
    MOZ_ASSERT(aItem == mPopups,
               "Unexpected - popup with no child not at end of chain");
    mPopups = std::move(parent);
  }
}

nsMenuChainItem* nsXULPopupManager::FirstMatchingPopup(
    mozilla::FunctionRef<bool(nsMenuChainItem*)> aMatcher) const {
  for (nsMenuChainItem* popup = mPopups.get(); popup;
       popup = popup->GetParent()) {
    if (aMatcher(popup)) {
      return popup;
    }
  }
  return nullptr;
}

void nsMenuChainItem::UpdateFollowAnchor() {
  mFollowAnchor = mFrame->ShouldFollowAnchor(mCurrentRect);
}

void nsMenuChainItem::CheckForAnchorChange() {
  if (mFollowAnchor) {
    mFrame->CheckForAnchorChange(mCurrentRect);
  }
}

NS_IMPL_ISUPPORTS(nsXULPopupManager, nsIDOMEventListener, nsIObserver)

nsXULPopupManager::nsXULPopupManager()
    : mActiveMenuBar(nullptr), mPopups(nullptr), mPendingPopup(nullptr) {
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->AddObserver(this, "xpcom-shutdown", false);
  }
}

nsXULPopupManager::~nsXULPopupManager() {
  NS_ASSERTION(!mPopups, "XUL popups still open");

  if (mNativeMenu) {
    mNativeMenu->RemoveObserver(this);
  }
}

void nsXULPopupManager::Init() {
  sInstance = do_AddRef(new nsXULPopupManager());
}

void nsXULPopupManager::Shutdown() { sInstance = nullptr; }

NS_IMETHODIMP
nsXULPopupManager::Observe(nsISupports* aSubject, const char* aTopic,
                           const char16_t* aData) {
  if (!nsCRT::strcmp(aTopic, "xpcom-shutdown")) {
    if (mKeyListener) {
      mKeyListener->RemoveEventListener(u"keypress"_ns, this, true);
      mKeyListener->RemoveEventListener(u"keydown"_ns, this, true);
      mKeyListener->RemoveEventListener(u"keyup"_ns, this, true);
      mKeyListener = nullptr;
    }
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, "xpcom-shutdown");
    }
  }

  return NS_OK;
}

nsXULPopupManager* nsXULPopupManager::GetInstance() {
  MOZ_ASSERT(sInstance);
  return sInstance;
}

bool nsXULPopupManager::RollupTooltips() {
  const RollupOptions options{0, nullptr, AllowAnimations::No};
  return RollupInternal(RollupKind::Tooltip, options, nullptr);
}

bool nsXULPopupManager::Rollup(const RollupOptions& aOptions,
                               nsIContent** aLastRolledUp) {
  return RollupInternal(RollupKind::Menu, aOptions, aLastRolledUp);
}

bool nsXULPopupManager::RollupNativeMenu() {
  if (mNativeMenu) {
    RefPtr<NativeMenu> menu = mNativeMenu;
    return menu->Close();
  }
  return false;
}

bool nsXULPopupManager::RollupInternal(RollupKind aKind,
                                       const RollupOptions& aOptions,
                                       nsIContent** aLastRolledUp) {
  if (aLastRolledUp) {
    *aLastRolledUp = nullptr;
  }

  // We can disable the autohide behavior via a pref to ease debugging.
  if (StaticPrefs::ui_popup_disable_autohide()) {
    // Required on linux to allow events to work on other targets.
    if (mWidget) {
      mWidget->CaptureRollupEvents(false);
    }
    return false;
  }

  nsMenuChainItem* item = GetRollupItem(aKind);
  if (!item) {
    return false;
  }
  if (aLastRolledUp) {
    // We need to get the popup that will be closed last, so that widget can
    // keep track of it so it doesn't reopen if a mousedown event is going to
    // processed. Keep going up the menu chain to get the first level menu of
    // the same type. If a different type is encountered it means we have,
    // for example, a menulist or context menu inside a panel, and we want to
    // treat these as distinct. It's possible that this menu doesn't end up
    // closing because the popuphiding event was cancelled, but in that case
    // we don't need to deal with the menu reopening as it will already still
    // be open.
    nsMenuChainItem* first = item;
    while (first->GetParent()) {
      nsMenuChainItem* parent = first->GetParent();
      if (first->Frame()->GetPopupType() != parent->Frame()->GetPopupType() ||
          first->IsContextMenu() != parent->IsContextMenu()) {
        break;
      }
      first = parent;
    }

    *aLastRolledUp = first->Element();
  }

  ConsumeOutsideClicksResult consumeResult =
      item->Frame()->ConsumeOutsideClicks();
  bool consume = consumeResult == ConsumeOutsideClicks_True;
  bool rollup = true;

  // If norolluponanchor is true, then don't rollup when clicking the anchor.
  // This would be used to allow adjusting the caret position in an
  // autocomplete field without hiding the popup for example.
  bool noRollupOnAnchor =
      (!consume && aOptions.mPoint &&
       item->Frame()->GetContent()->AsElement()->AttrValueIs(
           kNameSpaceID_None, nsGkAtoms::norolluponanchor, nsGkAtoms::_true,
           eCaseMatters));

  // When ConsumeOutsideClicks_ParentOnly is used, always consume the click
  // when the click was over the anchor. This way, clicking on a menu doesn't
  // reopen the menu.
  if ((consumeResult == ConsumeOutsideClicks_ParentOnly || noRollupOnAnchor) &&
      aOptions.mPoint) {
    nsMenuPopupFrame* popupFrame = item->Frame();
    CSSIntRect anchorRect = [&] {
      if (popupFrame->IsAnchored()) {
        // Check if the popup has an anchor rectangle set. If not, get the
        // rectangle from the anchor element.
        auto r = popupFrame->GetScreenAnchorRect();
        if (r.x != -1 && r.y != -1) {
          // Prefer the untransformed anchor rect, so as to account for Wayland
          // properly. Note we still need to check GetScreenAnchorRect() tho, so
          // as to detect whether the anchor came from the popup opening call,
          // or from an element (in which case we want to take the code-path
          // below)..
          auto untransformed = popupFrame->GetUntransformedAnchorRect();
          if (!untransformed.IsEmpty()) {
            return CSSIntRect::FromAppUnitsRounded(untransformed);
          }
          return r;
        }
      }

      auto* anchor = Element::FromNodeOrNull(popupFrame->GetAnchor());
      if (!anchor) {
        return CSSIntRect();
      }

      // Check if the anchor has indicated another node to use for checking
      // for roll-up. That way, we can anchor a popup on anonymous content
      // or an individual icon, while clicking elsewhere within a button or
      // other container doesn't result in us re-opening the popup.
      nsAutoString consumeAnchor;
      anchor->GetAttr(nsGkAtoms::consumeanchor, consumeAnchor);
      if (!consumeAnchor.IsEmpty()) {
        if (Element* newAnchor =
                anchor->OwnerDoc()->GetElementById(consumeAnchor)) {
          anchor = newAnchor;
        }
      }

      nsIFrame* f = anchor->GetPrimaryFrame();
      if (!f) {
        return CSSIntRect();
      }
      return f->GetScreenRect();
    }();

    // It's possible that some other element is above the anchor at the same
    // position, but the only thing that would happen is that the mouse
    // event will get consumed, so here only a quick coordinates check is
    // done rather than a slower complete check of what is at that location.
    nsPresContext* presContext = item->Frame()->PresContext();
    CSSIntPoint posCSSPixels =
        presContext->DevPixelsToIntCSSPixels(*aOptions.mPoint);
    if (anchorRect.Contains(posCSSPixels)) {
      if (consumeResult == ConsumeOutsideClicks_ParentOnly) {
        consume = true;
      }

      if (noRollupOnAnchor) {
        rollup = false;
      }
    }
  }

  if (!rollup) {
    return false;
  }

  // If a number of popups to close has been specified, determine the last
  // popup to close.
  Element* lastPopup = nullptr;
  uint32_t count = aOptions.mCount;
  if (count && count != UINT32_MAX) {
    nsMenuChainItem* last = item;
    while (--count && last->GetParent()) {
      last = last->GetParent();
    }
    if (last) {
      lastPopup = last->Element();
    }
  }

  HidePopupOptions options{HidePopupOption::HideChain,
                           HidePopupOption::DeselectMenu,
                           HidePopupOption::IsRollup};
  if (aOptions.mAllowAnimations == AllowAnimations::No) {
    options += HidePopupOption::DisableAnimations;
  }

  HidePopup(item->Element(), options, lastPopup);

  return consume;
}

////////////////////////////////////////////////////////////////////////
bool nsXULPopupManager::ShouldRollupOnMouseWheelEvent() {
  // should rollup only for autocomplete widgets
  // XXXndeakin this should really be something the popup has more control over

  nsMenuChainItem* item = GetTopVisibleMenu();
  if (!item) {
    return false;
  }

  nsIContent* content = item->Frame()->GetContent();
  if (!content || !content->IsElement()) {
    return false;
  }

  Element* element = content->AsElement();
  if (element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::rolluponmousewheel,
                           nsGkAtoms::_true, eCaseMatters)) {
    return true;
  }

  if (element->AttrValueIs(kNameSpaceID_None, nsGkAtoms::rolluponmousewheel,
                           nsGkAtoms::_false, eCaseMatters)) {
    return false;
  }

  nsAutoString value;
  element->GetAttr(nsGkAtoms::type, value);
  return StringBeginsWith(value, u"autocomplete"_ns);
}

bool nsXULPopupManager::ShouldConsumeOnMouseWheelEvent() {
  nsMenuChainItem* item = GetTopVisibleMenu();
  if (!item) {
    return false;
  }

  nsMenuPopupFrame* frame = item->Frame();
  if (frame->GetPopupType() != PopupType::Panel) {
    return true;
  }

  return !frame->GetContent()->AsElement()->AttrValueIs(
      kNameSpaceID_None, nsGkAtoms::type, nsGkAtoms::arrow, eCaseMatters);
}

// a menu should not roll up if activated by a mouse activate message (eg.
// X-mouse)
bool nsXULPopupManager::ShouldRollupOnMouseActivate() { return false; }

uint32_t nsXULPopupManager::GetSubmenuWidgetChain(
    nsTArray<nsIWidget*>* aWidgetChain) {
  // this method is used by the widget code to determine the list of popups
  // that are open. If a mouse click occurs outside one of these popups, the
  // panels will roll up. If the click is inside a popup, they will not roll up
  uint32_t count = 0, sameTypeCount = 0;

  NS_ASSERTION(aWidgetChain, "null parameter");
  nsMenuChainItem* item = GetTopVisibleMenu();
  while (item) {
    nsMenuChainItem* parent = item->GetParent();
    if (!item->IsNoAutoHide()) {
      nsCOMPtr<nsIWidget> widget = item->Frame()->GetWidget();
      NS_ASSERTION(widget, "open popup has no widget");
      if (widget) {
        aWidgetChain->AppendElement(widget.get());
        // In the case when a menulist inside a panel is open, clicking in the
        // panel should still roll up the menu, so if a different type is found,
        // stop scanning.
        if (!sameTypeCount) {
          count++;
          if (!parent ||
              item->Frame()->GetPopupType() !=
                  parent->Frame()->GetPopupType() ||
              item->IsContextMenu() != parent->IsContextMenu()) {
            sameTypeCount = count;
          }
        }
      }
    }
    item = parent;
  }

  return sameTypeCount;
}

nsIWidget* nsXULPopupManager::GetRollupWidget() {
  nsMenuChainItem* item = GetTopVisibleMenu();
  return item ? item->Frame()->GetWidget() : nullptr;
}

void nsXULPopupManager::AdjustPopupsOnWindowChange(
    nsPIDOMWindowOuter* aWindow) {
  // When the parent window is moved, adjust any child popups. Dismissable
  // menus and panels are expected to roll up when a window is moved, so there
  // is no need to check these popups, only the noautohide popups.

  // The items are added to a list so that they can be adjusted bottom to top.
  AutoTArray<nsMenuPopupFrame*, 8> list;
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    // only move popups that are within the same window and where auto
    // positioning has not been disabled
    if (!item->IsNoAutoHide()) {
      continue;
    }
    nsMenuPopupFrame* frame = item->Frame();
    nsPIDOMWindowOuter* window = frame->PresContext()->Document()->GetWindow();
    if (!window) {
      continue;
    }
    window = window->GetPrivateRoot();
    if (window == aWindow) {
      list.AppendElement(frame);
    }
  }

  for (nsMenuPopupFrame* popup : Reversed(list)) {
    popup->SetPopupPosition(true);
    popup->SchedulePendingWidgetMoveResize();
  }
}

void nsXULPopupManager::AdjustPopupsOnWindowChange(PresShell* aPresShell) {
  if (aPresShell->GetDocument()) {
    AdjustPopupsOnWindowChange(aPresShell->GetDocument()->GetWindow());
  }
}

nsMenuPopupFrame* nsXULPopupManager::GetPopupFrameForContent(
    nsIContent* aContent, bool aShouldFlush) {
  if (aShouldFlush) {
    Document* document = aContent->GetUncomposedDoc();
    if (document) {
      if (RefPtr<PresShell> presShell = document->GetPresShell()) {
        presShell->FlushPendingNotifications(FlushType::Layout);
      }
    }
  }

  return do_QueryFrame(aContent->GetPrimaryFrame());
}

nsMenuChainItem* nsXULPopupManager::GetRollupItem(RollupKind aKind) {
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    if (item->Frame()->PopupState() == ePopupInvisible) {
      continue;
    }
    MOZ_ASSERT_IF(item->Frame()->GetPopupType() == PopupType::Tooltip,
                  item->IsNoAutoHide());
    const bool valid = aKind == RollupKind::Tooltip
                           ? item->Frame()->GetPopupType() == PopupType::Tooltip
                           : !item->IsNoAutoHide();
    if (valid) {
      return item;
    }
  }
  return nullptr;
}

void nsXULPopupManager::SetActiveMenuBar(XULMenuBarElement* aMenuBar,
                                         bool aActivate) {
  if (aActivate) {
    mActiveMenuBar = aMenuBar;
  } else if (mActiveMenuBar == aMenuBar) {
    mActiveMenuBar = nullptr;
  }
  UpdateKeyboardListeners();
}

static CloseMenuMode GetCloseMenuMode(nsIContent* aMenu) {
  if (!aMenu->IsElement()) {
    return CloseMenuMode_Auto;
  }

  static Element::AttrValuesArray strings[] = {nsGkAtoms::none,
                                               nsGkAtoms::single, nullptr};
  switch (aMenu->AsElement()->FindAttrValueIn(
      kNameSpaceID_None, nsGkAtoms::closemenu, strings, eCaseMatters)) {
    case 0:
      return CloseMenuMode_None;
    case 1:
      return CloseMenuMode_Single;
    default:
      return CloseMenuMode_Auto;
  }
}

auto nsXULPopupManager::MayShowMenu(nsIContent* aMenu) -> MayShowMenuResult {
  if (mNativeMenu && aMenu->IsElement() &&
      mNativeMenu->Element()->Contains(aMenu)) {
    return {true};
  }

  auto* menu = XULButtonElement::FromNode(aMenu);
  if (!menu) {
    return {};
  }

  nsMenuPopupFrame* popupFrame = menu->GetMenuPopup(FlushType::None);
  if (!popupFrame || !MayShowPopup(popupFrame)) {
    return {};
  }
  return {false, menu, popupFrame};
}

static bool ShouldUseNativeAnchoredMenus() {
#ifdef HAS_NATIVE_MENU_SUPPORT
  return mozilla::widget::NativeMenuSupport::ShouldUseNativeAnchoredMenus();
#else
  return false;
#endif
}

static bool ShouldUseNativeContextMenus() {
#ifdef HAS_NATIVE_MENU_SUPPORT
  return mozilla::widget::NativeMenuSupport::ShouldUseNativeContextMenus();
#else
  return false;
#endif
}

void nsXULPopupManager::ShowMenu(nsIContent* aMenu, bool aSelectFirstItem) {
  auto mayShowResult = MayShowMenu(aMenu);
  if (NS_WARN_IF(!mayShowResult)) {
    return;
  }

  if (mayShowResult.mIsNative) {
    mNativeMenu->OpenSubmenu(aMenu->AsElement());
    return;
  }

  nsMenuPopupFrame* popupFrame = mayShowResult.mMenuPopupFrame;

  // inherit whether or not we're a context menu from the parent
  const bool onMenuBar = mayShowResult.mMenuButton->IsOnMenuBar();
  const bool onmenu = mayShowResult.mMenuButton->IsOnMenu();
  const bool parentIsContextMenu = mayShowResult.mMenuButton->IsOnContextMenu();

  nsAutoString position;

#ifdef XP_MACOSX
  if (aMenu->IsXULElement(nsGkAtoms::menulist)) {
    position.AssignLiteral("selection");
  } else
#endif

      if (onMenuBar || !onmenu) {
    position.AssignLiteral("after_start");
  } else {
    position.AssignLiteral("end_before");
  }

  RefPtr popup = &popupFrame->PopupElement();
  if ((!parentIsContextMenu || ShouldUseNativeContextMenus()) &&
      ShowPopupAtAnchorAsNativeMenu(aMenu, popup, position, true, nullptr)) {
    return;
  }

  // there is no trigger event for menus
  popupFrame->InitializePopup(aMenu, nullptr, position, 0, 0,
                              MenuPopupAnchorType::Node, true,
                              IsNativeMenu::No);
  PendingPopup pendingPopup(popup, nullptr);
  BeginShowingPopup(pendingPopup, parentIsContextMenu, aSelectFirstItem);
}

bool nsXULPopupManager::ShowNativeMenuInternal(
    Element* aPopup, nsIFrame* aClickedFrame, Event* aTriggerEvent,
    mozilla::FunctionRef<void(nsMenuPopupFrame*, nsIContent*)> aInitFn,
    mozilla::FunctionRef<void(NativeMenu*, nsMenuPopupFrame*, nsIFrame*)>
        aShowFn) {
  if (!aPopup->IsXULElement(nsGkAtoms::menupopup)) {
    // NativeMenu supports only menupopup elements.
    return false;
  }

  if (aPopup->GetBoolAttr(nsGkAtoms::nonnative)) {
    return false;
  }

  // Close any existing native menu
  if (mNativeMenu) {
    NS_WARNING("Native menu still open when trying to open another");
    RefPtr<NativeMenu> menu = mNativeMenu;
    (void)menu->Close();
    menu->RemoveObserver(this);
    mNativeMenu = nullptr;
  }

  RefPtr<NativeMenu> menu;
#ifdef HAS_NATIVE_MENU_SUPPORT
  menu = mozilla::widget::NativeMenuSupport::CreateNativePopupMenu(aPopup);
#endif

  if (!menu) {
    return false;
  }

  // Hide the menu from our accessibility code so that we don't dispatch custom
  // accessibility notifications which would conflict with the system ones.
  aPopup->SetAttr(kNameSpaceID_None, nsGkAtoms::aria_hidden, u"true"_ns, true);

  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(aPopup, true);
  if (!popupFrame) {
    // If there is no frame, we cannot show a native or a non-native menu.
    return true;
  }

  PendingPopup pendingPopup(aPopup, aTriggerEvent);
  nsCOMPtr<nsIContent> triggerContent = pendingPopup.GetTriggerContent();

  aInitFn(popupFrame, triggerContent);

  RefPtr<nsPresContext> presContext = popupFrame->PresContext();
  nsEventStatus status = FirePopupShowingEvent(pendingPopup, presContext);

  if ((popupFrame = GetPopupFrameForContent(aPopup, true))) {
    // if the event was cancelled, don't open the popup, reset its state back
    // to closed and clear its trigger content.
    if (status == nsEventStatus_eConsumeNoDefault) {
      popupFrame->SetPopupState(ePopupClosed);
      popupFrame->ClearTriggerContent();
      popupFrame->ClearAnchorContent();
      return true;
    }

    mNativeMenu = menu;
    mNativeMenu->AddObserver(this);

    if (!aClickedFrame) {
      aClickedFrame =
          popupFrame->PresContext()->PresShell()->GetCurrentEventFrame();
      if (!aClickedFrame) {
        aClickedFrame = popupFrame->PresContext()->PresShell()->GetRootFrame();
      }
    }

    aShowFn(menu, popupFrame, aClickedFrame);
  }

  return true;
}

void nsXULPopupManager::ShowPopup(Element* aPopup, nsIContent* aAnchorContent,
                                  const nsAString& aPosition, int32_t aXPos,
                                  int32_t aYPos, bool aIsContextMenu,
                                  bool aAttributesOverride,
                                  bool aSelectFirstItem, Event* aTriggerEvent) {
  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(aPopup, true);
  if (!popupFrame || !MayShowPopup(popupFrame)) {
    return;
  }

#ifdef XP_MACOSX
  if (aAnchorContent) {
    if (ShowPopupAtAnchorAsNativeMenu(aAnchorContent, aPopup, aPosition,
                                      aAttributesOverride, aTriggerEvent)) {
      return;
    }
  } else {
    // Convert the relative point to a screen point for the native menu.
    CSSIntPoint screenPoint =
        CSSIntPoint(aXPos, aYPos) +
        popupFrame->PresShell()->GetRootFrame()->GetScreenRect().TopLeft();
    if (ShowPopupAtScreenAsNativeMenu(aPopup, screenPoint, aIsContextMenu,
                                      aTriggerEvent)) {
      return;
    }
  }
#endif

  PendingPopup pendingPopup(aPopup, aTriggerEvent);
  nsCOMPtr<nsIContent> triggerContent = pendingPopup.GetTriggerContent();

  popupFrame->InitializePopup(aAnchorContent, triggerContent, aPosition, aXPos,
                              aYPos, MenuPopupAnchorType::Node,
                              aAttributesOverride, IsNativeMenu::No);

  BeginShowingPopup(pendingPopup, aIsContextMenu, aSelectFirstItem);
}

bool nsXULPopupManager::ShowPopupAtAnchorAsNativeMenu(
    nsIContent* aAnchorContent, Element* aPopup, const nsAString& aPosition,
    bool aAttributesOverride, Event* aTriggerEvent) {
  if (!ShouldUseNativeAnchoredMenus()) {
#ifdef XP_MACOSX
    // When native menus are disabled, add the nonnative attribute so that
    // CSS can be conditionally applied where necessary.
    aPopup->SetBoolAttr(nsGkAtoms::nonnative, true);
#endif
    return false;
  }

  return ShowNativeMenuInternal(
      aPopup, aAnchorContent->GetPrimaryFrame(), aTriggerEvent,
      [&](nsMenuPopupFrame* popupFrame, nsIContent* triggerContent) {
        popupFrame->InitializePopup(aAnchorContent, triggerContent, aPosition,
                                    0, 0, MenuPopupAnchorType::Node,
                                    aAttributesOverride, IsNativeMenu::Yes);
      },
      [&](NativeMenu* menu, nsMenuPopupFrame* popupFrame,
          nsIFrame* clickedFrame) {
        menu->ShowMenuAnchored(clickedFrame, popupFrame);
      });
}

void nsXULPopupManager::ShowPopupAtScreen(Element* aPopup, int32_t aXPos,
                                          int32_t aYPos, bool aIsContextMenu,
                                          Event* aTriggerEvent) {
  if (ShowPopupAtScreenAsNativeMenu(aPopup, CSSIntPoint(aXPos, aYPos),
                                    aIsContextMenu, aTriggerEvent)) {
    return;
  }

  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(aPopup, true);
  if (!popupFrame || !MayShowPopup(popupFrame)) {
    return;
  }

  PendingPopup pendingPopup(aPopup, aTriggerEvent);
  nsCOMPtr<nsIContent> triggerContent = pendingPopup.GetTriggerContent();

  popupFrame->InitializePopupAtScreen(triggerContent, aXPos, aYPos,
                                      aIsContextMenu, IsNativeMenu::No);
  BeginShowingPopup(pendingPopup, aIsContextMenu, false);
}

void ToggleTouchMode(const PendingPopup& aPopup) {
  aPopup.mPopup->SetBoolAttr(
      nsGkAtoms::touchmode,
      aPopup.MouseInputSource() == MouseEvent_Binding::MOZ_SOURCE_TOUCH);
}

bool nsXULPopupManager::ShowPopupAtScreenAsNativeMenu(Element* aPopup,
                                                      CSSIntPoint aScreenPoint,
                                                      bool aIsContextMenu,
                                                      Event* aTriggerEvent) {
  if (!ShouldUseNativeContextMenus()) {
    return false;
  }

  return ShowNativeMenuInternal(
      aPopup, nullptr, aTriggerEvent,
      [&](nsMenuPopupFrame* popupFrame, nsIContent* triggerContent) {
        popupFrame->InitializePopupAtScreen(triggerContent, aScreenPoint.x,
                                            aScreenPoint.y, aIsContextMenu,
                                            IsNativeMenu::Yes);
      },
      [&](NativeMenu* menu, nsMenuPopupFrame* popupFrame,
          nsIFrame* clickedFrame) {
        menu->ShowMenuAtPosition(clickedFrame, aScreenPoint, aIsContextMenu);
      });
}

void nsXULPopupManager::OnNativeMenuOpened() {
  if (!mNativeMenu) {
    return;
  }

  RefPtr<nsXULPopupManager> kungFuDeathGrip(this);

  nsCOMPtr<nsIContent> popup = mNativeMenu->Element();
  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(popup, true);
  if (popupFrame) {
    if (RefPtr menu = popupFrame->PopupElement().GetContainingMenu()) {
      menu->PopupOpened();
    }
    popupFrame->SetPopupState(ePopupShown);
  }

  // While the native menu is open, it consumes mouseup events.
  // Clear any :active state, mouse capture state and drag tracking now.
  EventStateManager* activeESM = static_cast<EventStateManager*>(
      EventStateManager::GetActiveEventStateManager());
  if (activeESM) {
    EventStateManager::ClearGlobalActiveContent(activeESM);
    activeESM->StopTrackingDragGesture(true);
  }

  PointerLockManager::Unlock("ShowNativeMenuInternal");
  PresShell::ReleaseCapturingContent();
}

void nsXULPopupManager::OnNativeMenuClosed() {
  if (!mNativeMenu) {
    return;
  }

  RefPtr<nsXULPopupManager> kungFuDeathGrip(this);

  bool shouldHideChain =
      mNativeMenuActivatedItemCloseMenuMode == Some(CloseMenuMode_Auto);

  nsCOMPtr<nsIContent> popup = mNativeMenu->Element();
  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(popup, true);
  if (popupFrame) {
    if (RefPtr menu = popupFrame->PopupElement().GetContainingMenu()) {
      menu->PopupClosed(false);
    }
    popupFrame->ClearTriggerContentIncludingDocument();
    popupFrame->ClearAnchorContent();
    popupFrame->SetPopupState(ePopupClosed);
  }
  mNativeMenu->RemoveObserver(this);
  mNativeMenu = nullptr;
  mNativeMenuActivatedItemCloseMenuMode = Nothing();
  mNativeMenuSubmenuStates.Clear();

  // Stop hiding the menu from accessibility code, in case it gets opened as a
  // non-native menu in the future.
  popup->AsElement()->UnsetAttr(kNameSpaceID_None, nsGkAtoms::aria_hidden,
                                true);

  if (shouldHideChain && mPopups &&
      mPopups->GetPopupType() == PopupType::Menu) {
    // A menu item was activated before this menu closed, and the item requested
    // the entire popup chain to be closed, which includes any open non-native
    // menus.
    // Close the non-native menus now. This matches the HidePopup call in
    // nsXULMenuCommandEvent::Run.
    HidePopup(mPopups->Element(), {HidePopupOption::HideChain});
  }
}

void nsXULPopupManager::OnNativeSubMenuWillOpen(
    mozilla::dom::Element* aPopupElement) {
  mNativeMenuSubmenuStates.InsertOrUpdate(aPopupElement, ePopupShowing);
}

void nsXULPopupManager::OnNativeSubMenuDidOpen(
    mozilla::dom::Element* aPopupElement) {
  mNativeMenuSubmenuStates.InsertOrUpdate(aPopupElement, ePopupShown);
}

void nsXULPopupManager::OnNativeSubMenuClosed(
    mozilla::dom::Element* aPopupElement) {
  mNativeMenuSubmenuStates.Remove(aPopupElement);
}

void nsXULPopupManager::OnNativeMenuWillActivateItem(
    mozilla::dom::Element* aMenuItemElement) {
  if (!mNativeMenu) {
    return;
  }

  CloseMenuMode cmm = GetCloseMenuMode(aMenuItemElement);
  mNativeMenuActivatedItemCloseMenuMode = Some(cmm);

  if (cmm == CloseMenuMode_Auto) {
    // If any non-native menus are visible (for example because the context menu
    // was opened on a non-native menu item, e.g. in a bookmarks folder), hide
    // the non-native menus before executing the item.
    HideOpenMenusBeforeExecutingMenu(CloseMenuMode_Auto);
  }
}

void nsXULPopupManager::ShowPopupAtScreenRect(
    Element* aPopup, const nsAString& aPosition, const nsIntRect& aRect,
    bool aIsContextMenu, bool aAttributesOverride, Event* aTriggerEvent) {
  if (ShowPopupAtScreenRectAsNativeMenu(
          aPopup, aPosition,
          CSSIntRect(aRect.x, aRect.y, aRect.width, aRect.height),
          aAttributesOverride, aTriggerEvent)) {
    // Native context menus don't anchor to a rect, so aIsContextMenu is not
    // passed here.
    return;
  }

  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(aPopup, true);
  if (!popupFrame || !MayShowPopup(popupFrame)) {
    return;
  }

  PendingPopup pendingPopup(aPopup, aTriggerEvent);
  nsCOMPtr<nsIContent> triggerContent = pendingPopup.GetTriggerContent();

  popupFrame->InitializePopupAtRect(triggerContent, aPosition, aRect,
                                    aAttributesOverride, IsNativeMenu::No);

  BeginShowingPopup(pendingPopup, aIsContextMenu, false);
}

bool nsXULPopupManager::ShowPopupAtScreenRectAsNativeMenu(
    Element* aPopup, const nsAString& aPosition, const CSSIntRect& aRect,
    bool aAttributesOverride, Event* aTriggerEvent) {
  if (!ShouldUseNativeAnchoredMenus()) {
    return false;
  }

  return ShowNativeMenuInternal(
      aPopup, nullptr, aTriggerEvent,
      [&](nsMenuPopupFrame* popupFrame, nsIContent* triggerContent) {
        popupFrame->InitializePopupAtRect(
            triggerContent, aPosition, aRect.ToUnknownRect(),
            aAttributesOverride, IsNativeMenu::Yes);
      },
      [&](NativeMenu* menu, nsMenuPopupFrame* popupFrame,
          nsIFrame* clickedFrame) {
        menu->ShowMenuAnchored(clickedFrame, popupFrame);
      });
}

void nsXULPopupManager::ShowTooltipAtScreen(
    Element* aPopup, nsIContent* aTriggerContent,
    const LayoutDeviceIntPoint& aScreenPoint) {
  nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(aPopup, true);
  if (!popupFrame || !MayShowPopup(popupFrame)) {
    return;
  }

  PendingPopup pendingPopup(aPopup, nullptr);

  nsPresContext* pc = popupFrame->PresContext();
  pendingPopup.SetMousePoint([&] {
    // Event coordinates are relative to the root widget
    if (nsPresContext* rootPresContext = pc->GetRootPresContext()) {
      if (nsCOMPtr<nsIWidget> rootWidget = rootPresContext->GetRootWidget()) {
        return aScreenPoint - rootWidget->WidgetToScreenOffset();
      }
    }
    return aScreenPoint;
  }());

  auto screenCSSPoint =
      CSSIntPoint::Round(aScreenPoint / pc->CSSToDevPixelScale());
  popupFrame->InitializePopupAtScreen(aTriggerContent, screenCSSPoint.x,
                                      screenCSSPoint.y, false,
                                      IsNativeMenu::No);

  BeginShowingPopup(pendingPopup, false, false);
}

static void CheckCaretDrawingState() {
  // There is 1 caret per document, we need to find the focused
  // document and erase its caret.
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm) {
    nsCOMPtr<mozIDOMWindowProxy> window;
    fm->GetFocusedWindow(getter_AddRefs(window));
    if (!window) {
      return;
    }

    auto* piWindow = nsPIDOMWindowOuter::From(window);
    MOZ_ASSERT(piWindow);

    nsCOMPtr<Document> focusedDoc = piWindow->GetDoc();
    if (!focusedDoc) {
      return;
    }

    PresShell* presShell = focusedDoc->GetPresShell();
    if (!presShell) {
      return;
    }

    RefPtr<nsCaret> caret = presShell->GetCaret();
    if (!caret) {
      return;
    }
    caret->SchedulePaint();
  }
}

void nsXULPopupManager::ShowPopupCallback(Element* aPopup,
                                          nsMenuPopupFrame* aPopupFrame,
                                          bool aIsContextMenu,
                                          bool aSelectFirstItem) {
  PopupType popupType = aPopupFrame->GetPopupType();
  const bool isMenu = popupType == PopupType::Menu;

  // Popups normally hide when an outside click occurs. Panels may use
  // the noautohide attribute to disable this behaviour. It is expected
  // that the application will hide these popups manually. The tooltip
  // listener will handle closing the tooltip also.
  bool isNoAutoHide =
      aPopupFrame->IsNoAutoHide() || popupType == PopupType::Tooltip;

  auto item = MakeUnique<nsMenuChainItem>(aPopupFrame, isNoAutoHide,
                                          aIsContextMenu, popupType);

  // install keyboard event listeners for navigating menus. For panels, the
  // escape key may be used to close the panel. However, the ignorekeys
  // attribute may be used to disable adding these event listeners for popups
  // that want to handle their own keyboard events.
  nsAutoString ignorekeys;
  aPopup->GetAttr(nsGkAtoms::ignorekeys, ignorekeys);
  if (ignorekeys.EqualsLiteral("true")) {
    item->SetIgnoreKeys(eIgnoreKeys_True);
  } else if (ignorekeys.EqualsLiteral("shortcuts")) {
    item->SetIgnoreKeys(eIgnoreKeys_Shortcuts);
  }

  if (isMenu) {
    // if the menu is on a menubar, use the menubar's listener instead
    if (auto* menu = aPopupFrame->PopupElement().GetContainingMenu()) {
      item->SetOnMenuBar(menu->IsOnMenuBar());
    }
  }

  // use a weak frame as the popup will set an open attribute if it is a menu
  AutoWeakFrame weakFrame(aPopupFrame);
  aPopupFrame->ShowPopup(aIsContextMenu);
  NS_ENSURE_TRUE_VOID(weakFrame.IsAlive());

  item->UpdateFollowAnchor();

  AddMenuChainItem(std::move(item));
  NS_ENSURE_TRUE_VOID(weakFrame.IsAlive());

  RefPtr popup = &aPopupFrame->PopupElement();
  popup->PopupOpened(aSelectFirstItem);

  if (isMenu) {
    UpdateMenuItems(aPopup);
  }

  // Caret visibility may have been affected, ensure that
  // the caret isn't now drawn when it shouldn't be.
  CheckCaretDrawingState();

  if (popupType != PopupType::Tooltip) {
    PointerLockManager::Unlock("ShowPopupCallback");
  }
}

nsMenuChainItem* nsXULPopupManager::FindPopup(Element* aPopup) const {
  auto matcher = [&](nsMenuChainItem* aItem) -> bool {
    return aItem->Frame()->GetContent() == aPopup;
  };
  return FirstMatchingPopup(matcher);
}

void nsXULPopupManager::HidePopup(Element* aPopup, HidePopupOptions aOptions,
                                  Element* aLastPopup) {
  if (mNativeMenu && mNativeMenu->Element() == aPopup) {
    RefPtr<NativeMenu> menu = mNativeMenu;
    (void)menu->Close();
    return;
  }

  nsMenuPopupFrame* popupFrame = do_QueryFrame(aPopup->GetPrimaryFrame());
  if (!popupFrame) {
    return;
  }

  nsMenuChainItem* foundPopup = FindPopup(aPopup);

  RefPtr<Element> popupToHide, nextPopup, lastPopup;

  if (foundPopup) {
    if (foundPopup->IsNoAutoHide()) {
      // If this is a noautohide panel, remove it but don't close any other
      // panels.
      popupToHide = aPopup;
      // XXX This preserves behavior but why is it the right thing to do?
      aOptions -= HidePopupOption::DeselectMenu;
    } else {
      // At this point, foundPopup will be set to the found item in the list. If
      // foundPopup is the topmost menu, the one to remove, then there are no
      // other popups to hide. If foundPopup is not the topmost menu, then there
      // may be open submenus below it. In this case, we need to make sure that
      // those submenus are closed up first. To do this, we scan up the menu
      // list to find the topmost popup with only menus between it and
      // foundPopup and close that menu first. In synchronous mode, the
      // FirePopupHidingEvent method will be called which in turn calls
      // HidePopupCallback to close up the next popup in the chain. These two
      // methods will be called in sequence recursively to close up all the
      // necessary popups. In asynchronous mode, a similar process occurs except
      // that the FirePopupHidingEvent method is called asynchronously. In
      // either case, nextPopup is set to the content node of the next popup to
      // close, and lastPopup is set to the last popup in the chain to close,
      // which will be aPopup, or null to close up all menus.

      nsMenuChainItem* topMenu = foundPopup;
      // Use IsMenu to ensure that foundPopup is a menu and scan down the child
      // list until a non-menu is found. If foundPopup isn't a menu at all,
      // don't scan and just close up this menu.
      if (foundPopup->IsMenu()) {
        nsMenuChainItem* child = foundPopup->GetChild();
        while (child && child->IsMenu()) {
          topMenu = child;
          child = child->GetChild();
        }
      }

      popupToHide = topMenu->Element();
      popupFrame = topMenu->Frame();

      const bool hideChain = aOptions.contains(HidePopupOption::HideChain);

      // Close up another popup if there is one, and we are either hiding the
      // entire chain or the item to hide isn't the topmost popup.
      nsMenuChainItem* parent = topMenu->GetParent();
      if (parent && (hideChain || topMenu != foundPopup)) {
        while (parent && parent->IsNoAutoHide()) {
          parent = parent->GetParent();
        }

        if (parent) {
          nextPopup = parent->Element();
        }
      }

      lastPopup = aLastPopup ? aLastPopup : (hideChain ? nullptr : aPopup);
    }
  } else if (popupFrame->PopupState() == ePopupPositioning) {
    // When the popup is in the popuppositioning state, it will not be in the
    // mPopups list. We need another way to find it and make sure it does not
    // continue the popup showing process.
    popupToHide = aPopup;
  }

  if (!popupToHide) {
    return;
  }

  nsPopupState state = popupFrame->PopupState();
  if (state == ePopupHiding) {
    // If the popup is already being hidden, don't fire another popuphiding
    // event. But finish hiding it sync if we need to.
    if (aOptions.contains(HidePopupOption::DisableAnimations) &&
        !aOptions.contains(HidePopupOption::Async)) {
      HidePopupCallback(popupToHide, popupFrame, nullptr, nullptr,
                        popupFrame->GetPopupType(), aOptions);
    }
    return;
  }

  // Change the popup state to hiding. Don't set the hiding state if the
  // popup is invisible, otherwise nsMenuPopupFrame::HidePopup will
  // run again. In the invisible state, we just want the events to fire.
  if (state != ePopupInvisible) {
    popupFrame->SetPopupState(ePopupHiding);
  }

  // For menus, popupToHide is always the frontmost item in the list to hide.
  if (aOptions.contains(HidePopupOption::Async)) {
    nsCOMPtr<nsIRunnable> event = MakeAndAddRef<nsXULPopupHidingEvent>(
        popupToHide, nextPopup, lastPopup, popupFrame->GetPopupType(),
        aOptions);
    aPopup->OwnerDoc()->Dispatch(event.forget());
  } else {
    RefPtr<nsPresContext> presContext = popupFrame->PresContext();
    FirePopupHidingEvent(popupToHide, nextPopup, lastPopup, presContext,
                         popupFrame->GetPopupType(), aOptions);
  }
}

void nsXULPopupManager::HideMenu(nsIContent* aMenu) {
  if (mNativeMenu && aMenu->IsElement() &&
      mNativeMenu->Element()->Contains(aMenu)) {
    mNativeMenu->CloseSubmenu(aMenu->AsElement());
    return;
  }

  auto* button = XULButtonElement::FromNode(aMenu);
  if (!button || !button->IsMenu()) {
    return;
  }
  auto* popup = button->GetMenuPopupContent();
  if (!popup) {
    return;
  }
  HidePopup(popup, {HidePopupOption::DeselectMenu});
}

// This is used to hide the popup after a transition finishes.
class TransitionEnder final : public nsIDOMEventListener {
 private:
  // Effectively const but is cycle collected
  MOZ_KNOWN_LIVE RefPtr<Element> mElement;

 protected:
  virtual ~TransitionEnder() = default;

 public:
  HidePopupOptions mOptions;

  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(TransitionEnder)

  TransitionEnder(Element* aElement, HidePopupOptions aOptions)
      : mElement(aElement), mOptions(aOptions) {}

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD HandleEvent(Event* aEvent) override {
    mElement->RemoveSystemEventListener(u"transitionend"_ns, this, false);
    mElement->RemoveSystemEventListener(u"transitioncancel"_ns, this, false);

    nsMenuPopupFrame* popupFrame = do_QueryFrame(mElement->GetPrimaryFrame());
    if (!popupFrame || popupFrame->PopupState() != ePopupHiding) {
      return NS_OK;
    }

    // Now hide the popup. There could be other properties transitioning, but
    // we'll assume they all end at the same time and just hide the popup upon
    // the first one ending.
    if (RefPtr<nsXULPopupManager> pm = nsXULPopupManager::GetInstance()) {
      pm->HidePopupCallback(mElement, popupFrame, nullptr, nullptr,
                            popupFrame->GetPopupType(), mOptions);
    }

    return NS_OK;
  }
};

NS_IMPL_CYCLE_COLLECTING_ADDREF(TransitionEnder)
NS_IMPL_CYCLE_COLLECTING_RELEASE(TransitionEnder)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TransitionEnder)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventListener)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(TransitionEnder, mElement);
void nsXULPopupManager::HidePopupCallback(
    Element* aPopup, nsMenuPopupFrame* aPopupFrame, Element* aNextPopup,
    Element* aLastPopup, PopupType aPopupType, HidePopupOptions aOptions) {
  if (mCloseTimer && mTimerMenu == aPopupFrame) {
    mCloseTimer->Cancel();
    mCloseTimer = nullptr;
    mTimerMenu = nullptr;
  }

  // The popup to hide is aPopup. Search the list again to find the item that
  // corresponds to the popup to hide aPopup. This is done because it's
  // possible someone added another item (attempted to open another popup)
  // or removed a popup frame during the event processing so the item isn't at
  // the front anymore.
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    if (item->Element() == aPopup) {
      RemoveMenuChainItem(item);
      SetCaptureState(aPopup);
      break;
    }
  }

  AutoWeakFrame weakFrame(aPopupFrame);
  aPopupFrame->HidePopup(aOptions.contains(HidePopupOption::DeselectMenu),
                         ePopupClosed);
  NS_ENSURE_TRUE_VOID(weakFrame.IsAlive());

  // send the popuphidden event synchronously. This event has no default
  // behaviour.
  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetMouseEvent event(true, eXULPopupHidden, nullptr,
                         WidgetMouseEvent::eReal);
  RefPtr<nsPresContext> presContext = aPopupFrame->PresContext();
  EventDispatcher::Dispatch(aPopup, presContext, &event, nullptr, &status);
  NS_ENSURE_TRUE_VOID(weakFrame.IsAlive());

  // Force any popups that might be anchored on elements within this popup to
  // update.
  UpdatePopupPositions(presContext->RefreshDriver());

  // if there are more popups to close, look for the next one
  if (aNextPopup && aPopup != aLastPopup) {
    nsMenuChainItem* foundMenu = FindPopup(aNextPopup);

    // continue hiding the chain of popups until the last popup aLastPopup
    // is reached, or until a popup of a different type is reached. This
    // last check is needed so that a menulist inside a non-menu panel only
    // closes the menu and not the panel as well.
    if (foundMenu && (aLastPopup || aPopupType == foundMenu->GetPopupType())) {
      nsCOMPtr<Element> popupToHide = foundMenu->Element();
      nsMenuChainItem* parent = foundMenu->GetParent();

      nsCOMPtr<Element> nextPopup;
      if (parent && popupToHide != aLastPopup) {
        nextPopup = parent->Element();
      }

      nsMenuPopupFrame* popupFrame = foundMenu->Frame();
      nsPopupState state = popupFrame->PopupState();
      if (state == ePopupHiding) {
        return;
      }
      if (state != ePopupInvisible) {
        popupFrame->SetPopupState(ePopupHiding);
      }

      RefPtr<nsPresContext> presContext = popupFrame->PresContext();
      FirePopupHidingEvent(popupToHide, nextPopup, aLastPopup, presContext,
                           foundMenu->GetPopupType(), aOptions);
    }
  }
}

void nsXULPopupManager::HidePopupAfterDelay(nsMenuPopupFrame* aPopup,
                                            int32_t aDelay) {
  // Don't close up immediately.
  // Kick off a close timer.
  KillMenuTimer();

  // Kick off the timer.
  nsIEventTarget* target = GetMainThreadSerialEventTarget();
  NS_NewTimerWithFuncCallback(
      getter_AddRefs(mCloseTimer),
      [](nsITimer* aTimer, void* aClosure) {
        if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
          pm->KillMenuTimer();
        }
      },
      nullptr, aDelay, nsITimer::TYPE_ONE_SHOT, "KillMenuTimer"_ns, target);
  // the popup will call PopupDestroyed if it is destroyed, which checks if it
  // is set to mTimerMenu, so it should be safe to keep a reference to it
  mTimerMenu = aPopup;
}

void nsXULPopupManager::HidePopupsInList(
    const nsTArray<nsMenuPopupFrame*>& aFrames) {
  // Create a weak frame list. This is done in a separate array with the
  // right capacity predetermined to avoid multiple allocations.
  nsTArray<WeakFrame> weakPopups(aFrames.Length());
  uint32_t f;
  for (f = 0; f < aFrames.Length(); f++) {
    WeakFrame* wframe = weakPopups.AppendElement();
    if (wframe) {
      *wframe = aFrames[f];
    }
  }

  for (f = 0; f < weakPopups.Length(); f++) {
    // check to ensure that the frame is still alive before hiding it.
    if (weakPopups[f].IsAlive()) {
      auto* frame = static_cast<nsMenuPopupFrame*>(weakPopups[f].GetFrame());
      frame->HidePopup(true, ePopupInvisible);
    }
  }

  SetCaptureState(nullptr);
}

bool nsXULPopupManager::IsChildOfDocShell(Document* aDoc,
                                          nsIDocShellTreeItem* aExpected) {
  nsCOMPtr<nsIDocShellTreeItem> docShellItem(aDoc->GetDocShell());
  while (docShellItem) {
    if (docShellItem == aExpected) {
      return true;
    }

    nsCOMPtr<nsIDocShellTreeItem> parent;
    docShellItem->GetInProcessParent(getter_AddRefs(parent));
    docShellItem = parent;
  }

  return false;
}

void nsXULPopupManager::HidePopupsInDocShell(
    nsIDocShellTreeItem* aDocShellToHide) {
  nsTArray<nsMenuPopupFrame*> popupsToHide;

  // Iterate to get the set of popup frames to hide
  nsMenuChainItem* item = mPopups.get();
  while (item) {
    // Get the parent before calling detach so that we can keep iterating.
    nsMenuChainItem* parent = item->GetParent();
    if (item->Frame()->PopupState() != ePopupInvisible &&
        IsChildOfDocShell(item->Element()->OwnerDoc(), aDocShellToHide)) {
      nsMenuPopupFrame* frame = item->Frame();
      RemoveMenuChainItem(item);
      popupsToHide.AppendElement(frame);
    }
    item = parent;
  }

  HidePopupsInList(popupsToHide);
}

void nsXULPopupManager::UpdatePopupPositions(nsRefreshDriver* aRefreshDriver) {
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    nsMenuPopupFrame* frame = item->Frame();
    if (frame->PresContext()->RefreshDriver() != aRefreshDriver) {
      continue;
    }
    item->CheckForAnchorChange();
  }
}

void nsXULPopupManager::PaintPopups(nsRefreshDriver* aRefreshDriver) {
  if (!mPopups) {
    return;
  }

  AutoTArray<std::pair<RefPtr<nsIWidget>, WeakFrame>, 32> visiblePopups;
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    nsMenuPopupFrame* frame = item->Frame();
    if (!frame->IsVisible() ||
        frame->PresContext()->GetRootPresContext()->RefreshDriver() !=
            aRefreshDriver) {
      continue;
    }
    if (nsIWidget* widget = frame->GetWidget()) {
      visiblePopups.AppendElement(std::make_pair(widget, frame));
    }
  }

  for (const auto& visiblePopup : Reversed(visiblePopups)) {
    nsIWidget* widget = visiblePopup.first;
    nsMenuPopupFrame* frame = do_QueryFrame(visiblePopup.second.GetFrame());
    if (!frame) {
      continue;
    }
    if (frame->PendingWidgetMoveResize()) {
      frame->ClearPendingWidgetMoveResize();

      LayoutDeviceIntRect curBounds = widget->GetClientBounds();
      auto newBounds = frame->CalcWidgetBounds();
      widget->ConstrainSize(&newBounds.width, &newBounds.height);
      const bool changedPos = curBounds.TopLeft() != newBounds.TopLeft();
      const bool changedSize = curBounds.Size() != newBounds.Size();

      if (changedPos || changedSize) {
        DesktopToLayoutDeviceScale scale = widget->GetDesktopToDeviceScale();
        DesktopRect deskRect = newBounds / scale;
        if (changedPos) {
          if (changedSize) {
            widget->ResizeClient(deskRect, true);
          } else {
            widget->MoveClient(deskRect.TopLeft());
          }
        } else if (changedSize) {
          widget->ResizeClient(deskRect.Size(), true);
        }
      }
    }
    if (!widget->IsVisible()) {
      widget->Show(true);
    }
    if (!visiblePopup.second.IsAlive() || !widget->NeedsPaint()) {
      continue;
    }
    nsAutoScriptBlocker scriptBlocker;
    RefPtr<PresShell> ps = frame->PresShell();
    RefPtr<WindowRenderer> renderer = widget->GetWindowRenderer();
    if (renderer->AsFallback()) {
      // FIXME: A bit of a hack. This matches what PaintAndRequestComposite
      // does for views (eventually).
      widget->Invalidate(LayoutDeviceIntRect({}, widget->GetBounds().Size()));
    } else {
      ps->PaintAndRequestComposite(frame, renderer, PaintFlags::None);
    }
  }
}

void nsXULPopupManager::UpdateFollowAnchor(nsMenuPopupFrame* aPopup) {
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    if (item->Frame() == aPopup) {
      item->UpdateFollowAnchor();
      break;
    }
  }
}

void nsXULPopupManager::HideOpenMenusBeforeExecutingMenu(CloseMenuMode aMode) {
  if (aMode == CloseMenuMode_None) {
    return;
  }

  // When a menuitem is selected to be executed, first hide all the open
  // popups, but don't remove them yet. This is needed when a menu command
  // opens a modal dialog. The views associated with the popups needed to be
  // hidden and the accesibility events fired before the command executes, but
  // the popuphiding/popuphidden events are fired afterwards.
  nsTArray<nsMenuPopupFrame*> popupsToHide;
  nsMenuChainItem* item = GetTopVisibleMenu();
  while (item) {
    // if it isn't a <menupopup>, don't close it automatically
    if (!item->IsMenu()) {
      break;
    }

    nsMenuChainItem* next = item->GetParent();
    popupsToHide.AppendElement(item->Frame());
    if (aMode == CloseMenuMode_Single) {
      // only close one level of menu
      break;
    }
    item = next;
  }

  // Now hide the popups. If the closemenu mode is auto, deselect the menu,
  // otherwise only one popup is closing, so keep the parent menu selected.
  HidePopupsInList(popupsToHide);
}

void nsXULPopupManager::ExecuteMenu(nsIContent* aMenu,
                                    nsXULMenuCommandEvent* aEvent) {
  CloseMenuMode cmm = GetCloseMenuMode(aMenu);
  HideOpenMenusBeforeExecutingMenu(cmm);
  aEvent->SetCloseMenuMode(cmm);
  nsCOMPtr<nsIRunnable> event = aEvent;
  aMenu->OwnerDoc()->Dispatch(event.forget());
}

bool nsXULPopupManager::ActivateNativeMenuItem(nsIContent* aItem,
                                               mozilla::Modifiers aModifiers,
                                               int16_t aButton,
                                               mozilla::ErrorResult& aRv) {
  if (mNativeMenu && aItem->IsElement() &&
      mNativeMenu->Element()->Contains(aItem)) {
    mNativeMenu->ActivateItem(aItem->AsElement(), aModifiers, aButton, aRv);
    return true;
  }
  return false;
}

nsEventStatus nsXULPopupManager::FirePopupShowingEvent(
    const PendingPopup& aPendingPopup, nsPresContext* aPresContext) {
  // Cache the pending popup so that the trigger node and other properties can
  // be retrieved during the popupshowing event. It will be cleared below after
  // the event has fired.
  AutoRestore<const PendingPopup*> restorePendingPopup(mPendingPopup);
  mPendingPopup = &aPendingPopup;

  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetMouseEvent event(true, eXULPopupShowing, nullptr,
                         WidgetMouseEvent::eReal);

  // coordinates are relative to the root widget
  nsPresContext* rootPresContext = aPresContext->GetRootPresContext();
  if (rootPresContext) {
    event.mWidget = rootPresContext->GetRootWidget();
  } else {
    event.mWidget = nullptr;
  }

  event.mInputSource = aPendingPopup.MouseInputSource();
  event.mRefPoint = aPendingPopup.mMousePoint;
  event.mModifiers = aPendingPopup.mModifiers;
  if (aPendingPopup.mEvent) {
    event.mTriggerEvent = aPendingPopup.mEvent;
  }
  RefPtr<nsIContent> popup = aPendingPopup.mPopup;
  EventDispatcher::Dispatch(popup, aPresContext, &event, nullptr, &status);

  return status;
}

void nsXULPopupManager::BeginShowingPopup(const PendingPopup& aPendingPopup,
                                          bool aIsContextMenu,
                                          bool aSelectFirstItem) {
  RefPtr<Element> popup = aPendingPopup.mPopup;

  nsMenuPopupFrame* popupFrame = do_QueryFrame(popup->GetPrimaryFrame());
  if (NS_WARN_IF(!popupFrame)) {
    return;
  }

  RefPtr<nsPresContext> presContext = popupFrame->PresContext();
  RefPtr<PresShell> presShell = presContext->PresShell();
  presShell->FrameNeedsReflow(popupFrame, IntrinsicDirty::FrameAndAncestors,
                              NS_FRAME_IS_DIRTY);

  PopupType popupType = popupFrame->GetPopupType();

  ToggleTouchMode(aPendingPopup);

  nsEventStatus status = FirePopupShowingEvent(aPendingPopup, presContext);

  // if a panel, blur whatever has focus so that the panel can take the focus.
  // This is done after the popupshowing event in case that event is cancelled.
  // Using noautofocus="true" will disable this behaviour, which is needed for
  // the autocomplete widget as it manages focus itself.
  if (popupType == PopupType::Panel &&
      !popup->GetBoolAttr(nsGkAtoms::noautofocus)) {
    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      Document* doc = popup->GetUncomposedDoc();

      // Only remove the focus if the currently focused item is ouside the
      // popup. It isn't a big deal if the current focus is in a child popup
      // inside the popup as that shouldn't be visible. This check ensures that
      // a node inside the popup that is focused during a popupshowing event
      // remains focused.
      RefPtr<Element> currentFocus = fm->GetFocusedElement();
      if (doc && currentFocus &&
          !nsContentUtils::ContentIsCrossDocDescendantOf(currentFocus, popup)) {
        nsCOMPtr<nsPIDOMWindowOuter> outerWindow = doc->GetWindow();
        fm->ClearFocus(outerWindow);
      }
    }
  }

  popup->OwnerDoc()->FlushPendingNotifications(FlushType::Frames);

  // get the frame again in case it went away
  popupFrame = do_QueryFrame(popup->GetPrimaryFrame());
  if (!popupFrame) {
    return;
  }
  // if the event was cancelled or the popup was closed in the mean time, don't
  // open the popup, reset its state back to closed and clear its trigger
  // content.
  if (popupFrame->PopupState() == ePopupClosed ||
      status == nsEventStatus_eConsumeNoDefault) {
    popupFrame->SetPopupState(ePopupClosed);
    popupFrame->ClearTriggerContent();
    return;
  }
  // Now check if we need to fire the popuppositioned event. If not, call
  // ShowPopupCallback directly.
  // The popuppositioned event only fires on arrow panels for now.
  if (popup->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type, nsGkAtoms::arrow,
                         eCaseMatters)) {
    popupFrame->ShowWithPositionedEvent();
    presShell->FrameNeedsReflow(popupFrame, IntrinsicDirty::FrameAndAncestors,
                                NS_FRAME_HAS_DIRTY_CHILDREN);
  } else {
    ShowPopupCallback(popup, popupFrame, aIsContextMenu, aSelectFirstItem);
  }
}

void nsXULPopupManager::FirePopupHidingEvent(Element* aPopup,
                                             Element* aNextPopup,
                                             Element* aLastPopup,
                                             nsPresContext* aPresContext,
                                             PopupType aPopupType,
                                             HidePopupOptions aOptions) {
  nsCOMPtr<nsIContent> popup = aPopup;
  RefPtr<PresShell> presShell = aPresContext->PresShell();
  (void)presShell;  // This presShell may be keeping things alive
                    // on non GTK platforms

  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetMouseEvent event(true, eXULPopupHiding, nullptr,
                         WidgetMouseEvent::eReal);
  EventDispatcher::Dispatch(aPopup, aPresContext, &event, nullptr, &status);

  // when a panel is closed, blur whatever has focus inside the popup
  if (aPopupType == PopupType::Panel &&
      !aPopup->GetBoolAttr(nsGkAtoms::noautofocus)) {
    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      Document* doc = aPopup->GetUncomposedDoc();

      // Remove the focus from the focused node only if it is inside the popup.
      RefPtr<Element> currentFocus = fm->GetFocusedElement();
      if (doc && currentFocus &&
          nsContentUtils::ContentIsCrossDocDescendantOf(currentFocus, aPopup)) {
        nsCOMPtr<nsPIDOMWindowOuter> outerWindow = doc->GetWindow();
        fm->ClearFocus(outerWindow);
      }
    }
  }

  aPopup->OwnerDoc()->FlushPendingNotifications(FlushType::Frames);

  // get frame again in case it went away
  nsMenuPopupFrame* popupFrame = do_QueryFrame(aPopup->GetPrimaryFrame());
  if (!popupFrame) {
    return;
  }

  // If the event was cancelled, don't hide the popup, and reset its
  // state back to open. Only popups in chrome shells can prevent a popup
  // from hiding.
  if (status == nsEventStatus_eConsumeNoDefault &&
      !popupFrame->IsInContentShell()) {
    // XXXndeakin
    // If an attempt was made to hide this popup before the popupshown event
    // fired, then ePopupShown is set here even though it should be
    // ePopupVisible. This probably isn't worth the hassle of handling.
    popupFrame->SetPopupState(ePopupShown);
    return;
  }

  const bool shouldAnimate = [&] {
    if (!LookAndFeel::GetInt(LookAndFeel::IntID::PanelAnimations)) {
      // Animations are not supported by the platform, avoid transitioning.
      return false;
    }
    if (aOptions.contains(HidePopupOption::DisableAnimations)) {
      // Animations are not allowed by our caller.
      return false;
    }
    if (aNextPopup) {
      // If there is a next popup, indicating that mutliple popups are rolling
      // up, don't wait and hide the popup right away since the effect would
      // likely be undesirable.
      return false;
    }
    nsAutoString animate;
    if (!aPopup->GetAttr(nsGkAtoms::animate, animate)) {
      return false;
    }
    // If animate="false" then don't transition at all.
    if (animate.EqualsLiteral("false")) {
      return false;
    }
    // If animate="cancel", only show the transition if cancelling the popup
    // or rolling up.
    if (animate.EqualsLiteral("cancel") &&
        !aOptions.contains(HidePopupOption::IsRollup)) {
      return false;
    }
    return true;
  }();
  // If we should animate the popup, check if it has a closing transition
  // and wait for it to finish.
  // The transition would still occur either way, but if we don't wait the
  // view will be hidden and you won't be able to see it.
  if (shouldAnimate && AnimationUtils::HasCurrentTransitions(aPopup)) {
    auto ender = MakeRefPtr<TransitionEnder>(aPopup, aOptions);
    aPopup->AddSystemEventListener(u"transitionend"_ns, ender, false, false);
    aPopup->AddSystemEventListener(u"transitioncancel"_ns, ender, false, false);
    return;
  }

  HidePopupCallback(aPopup, popupFrame, aNextPopup, aLastPopup, aPopupType,
                    aOptions);
}

bool nsXULPopupManager::IsPopupOpen(Element* aPopup) {
  if (mNativeMenu && mNativeMenu->Element() == aPopup) {
    return true;
  }

  // a popup is open if it is in the open list. The assertions ensure that the
  // frame is in the correct state. If the popup is in the hiding or invisible
  // state, it will still be in the open popup list until it is closed.
  if (nsMenuChainItem* item = FindPopup(aPopup)) {
    NS_ASSERTION(item->Frame()->IsOpen() ||
                     item->Frame()->PopupState() == ePopupHiding ||
                     item->Frame()->PopupState() == ePopupInvisible,
                 "popup in open list not actually open");
    (void)item;
    return true;
  }
  return false;
}

nsIFrame* nsXULPopupManager::GetTopPopup(PopupType aType) {
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    if (item->Frame()->IsVisible() &&
        (item->GetPopupType() == aType || aType == PopupType::Any)) {
      return item->Frame();
    }
  }
  return nullptr;
}

nsIContent* nsXULPopupManager::GetTopActiveMenuItemContent() {
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    if (!item->Frame()->IsVisible()) {
      continue;
    }
    if (auto* content = item->Frame()->PopupElement().GetActiveMenuChild()) {
      return content;
    }
  }
  return nullptr;
}

void nsXULPopupManager::GetVisiblePopups(nsTArray<nsMenuPopupFrame*>& aPopups,
                                         bool aIncludeNativeMenu) {
  aPopups.Clear();
  if (aIncludeNativeMenu && mNativeMenu) {
    nsCOMPtr<nsIContent> popup = mNativeMenu->Element();
    nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(popup, true);
    if (popupFrame && popupFrame->IsVisible() &&
        !popupFrame->IsMouseTransparent()) {
      aPopups.AppendElement(popupFrame);
    }
  }
  for (nsMenuChainItem* item = mPopups.get(); item; item = item->GetParent()) {
    // Skip panels which are not visible as well as popups that are transparent
    // to mouse events.
    if (item->Frame()->IsVisible() && !item->Frame()->IsMouseTransparent()) {
      aPopups.AppendElement(item->Frame());
    }
  }
}

already_AddRefed<nsINode> nsXULPopupManager::GetLastTriggerNode(
    Document* aDocument, bool aIsTooltip) {
  if (!aDocument) {
    return nullptr;
  }

  RefPtr<nsINode> node;

  // If a pending popup is set, it means that a popupshowing event is being
  // fired. In this case, just use the cached node, as the popup is not yet in
  // the list of open popups.
  RefPtr<nsIContent> openingPopup =
      mPendingPopup ? mPendingPopup->mPopup : nullptr;
  if (openingPopup && openingPopup->GetUncomposedDoc() == aDocument &&
      aIsTooltip == openingPopup->IsXULElement(nsGkAtoms::tooltip)) {
    node = nsMenuPopupFrame::GetTriggerContent(
        GetPopupFrameForContent(openingPopup, false));
  } else if (mNativeMenu && !aIsTooltip) {
    RefPtr<dom::Element> popup = mNativeMenu->Element();
    if (popup->GetUncomposedDoc() == aDocument) {
      nsMenuPopupFrame* popupFrame = GetPopupFrameForContent(popup, false);
      node = nsMenuPopupFrame::GetTriggerContent(popupFrame);
    }
  } else {
    for (nsMenuChainItem* item = mPopups.get(); item;
         item = item->GetParent()) {
      // look for a popup of the same type and document.
      if ((item->GetPopupType() == PopupType::Tooltip) == aIsTooltip &&
          item->Element()->GetUncomposedDoc() == aDocument) {
        node = nsMenuPopupFrame::GetTriggerContent(item->Frame());
        if (node) {
          break;
        }
      }
    }
  }

  return node.forget();
}

bool nsXULPopupManager::MayShowPopup(nsMenuPopupFrame* aPopup) {
  // if a popup's IsOpen method returns true, then the popup must always be in
  // the popup chain scanned in IsPopupOpen.
  NS_ASSERTION(!aPopup->IsOpen() || IsPopupOpen(&aPopup->PopupElement()),
               "popup frame state doesn't match XULPopupManager open state");

  nsPopupState state = aPopup->PopupState();

  // if the popup is not in the open popup chain, then it must have a state that
  // is either closed, in the process of being shown, or invisible.
  NS_ASSERTION(IsPopupOpen(&aPopup->PopupElement()) || state == ePopupClosed ||
                   state == ePopupShowing || state == ePopupPositioning ||
                   state == ePopupInvisible,
               "popup not in XULPopupManager open list is open");

  // don't show popups unless they are closed or invisible
  if (state != ePopupClosed && state != ePopupInvisible) {
    return false;
  }

  // Don't show popups that we already have in our popup chain
  if (IsPopupOpen(&aPopup->PopupElement())) {
    NS_WARNING("Refusing to show duplicate popup");
    return false;
  }

  // if the popup was just rolled up, don't reopen it
  if (mozilla::widget::nsAutoRollup::GetLastRollup() == aPopup->GetContent()) {
    return false;
  }

  if (mNativeMenu && aPopup->GetPopupType() == PopupType::Tooltip) {
    // Tooltips appearing while a native menu is open can obscure the menu and
    // cannot be dismissed until the menu is closed. Native menu items with
    // tooltips will use native tooltips anyway, which do not hit this path.
    return false;
  }

  nsCOMPtr<nsIDocShell> docShell = aPopup->PresContext()->GetDocShell();

  nsCOMPtr<nsIBaseWindow> baseWin = do_QueryInterface(docShell);
  if (!baseWin) {
    return false;
  }

  nsCOMPtr<nsIDocShellTreeItem> root;
  docShell->GetInProcessRootTreeItem(getter_AddRefs(root));
  if (!root) {
    return false;
  }

  nsCOMPtr<nsPIDOMWindowOuter> rootWin = root->GetWindow();

  MOZ_RELEASE_ASSERT(XRE_IsParentProcess(),
                     "Cannot have XUL in content process showing popups.");

  // chrome shells can always open popups, but other types of shells can only
  // open popups when they are focused and visible
  if (docShell->ItemType() != nsIDocShellTreeItem::typeChrome) {
    // only allow popups in active windows
    nsFocusManager* fm = nsFocusManager::GetFocusManager();
    if (!fm || !rootWin) {
      return false;
    }

    nsCOMPtr<nsPIDOMWindowOuter> activeWindow = fm->GetActiveWindow();
    if (activeWindow != rootWin) {
      return false;
    }

    // only allow popups in visible frames
    // TODO: This visibility check should be replaced with a check of
    // bc->IsActive(). It is okay for now since this is only called
    // in the parent process. Bug 1698533.
    bool visible;
    baseWin->GetVisibility(&visible);
    if (!visible) {
      return false;
    }
  }

  // platforms respond differently when an popup is opened in a minimized
  // window, so this is always disabled.
  nsCOMPtr<nsIWidget> mainWidget = baseWin->GetMainWidget();
  if (mainWidget && mainWidget->SizeMode() == nsSizeMode_Minimized) {
    return false;
  }

#ifdef XP_MACOSX
  if (rootWin) {
    auto globalWin = nsGlobalWindowOuter::Cast(rootWin.get());
    if (globalWin->IsInModalState()) {
      return false;
    }
  }
#endif

  // cannot open a popup that is a submenu of a menupopup that isn't open.
  if (auto* menu = aPopup->PopupElement().GetContainingMenu()) {
    if (auto* parent = XULPopupElement::FromNodeOrNull(menu->GetMenuParent())) {
      nsMenuPopupFrame* f = do_QueryFrame(parent->GetPrimaryFrame());
      if (f && !f->IsOpen()) {
        return false;
      }
    }
  }

  return true;
}

void nsXULPopupManager::PopupDestroyed(nsMenuPopupFrame* aPopup) {
  // when a popup frame is destroyed, just unhook it from the list of popups
  CancelMenuTimer(aPopup);

  nsMenuChainItem* item = FindPopup(&aPopup->PopupElement());
  if (!item) {
    return;
  }

  nsTArray<nsMenuPopupFrame*> popupsToHide;
  // XXXndeakin shouldn't this only happen for menus?
  if (!item->IsNoAutoHide() && item->Frame()->PopupState() != ePopupInvisible) {
    // Iterate through any child menus and hide them as well, since the
    // parent is going away. We won't remove them from the list yet, just
    // hide them, as they will be removed from the list when this function
    // gets called for that child frame.
    for (auto* child = item->GetChild(); child; child = child->GetChild()) {
      // If the popup is a child frame of the menu that was destroyed, add it
      // to the list of popups to hide. Don't bother with the events since the
      // frames are going away. If the child menu is not a child frame, for
      // example, a context menu, use HidePopup instead, but call it
      // asynchronously since we are in the middle of frame destruction.
      if (nsLayoutUtils::IsProperAncestorFrame(item->Frame(), child->Frame())) {
        popupsToHide.AppendElement(child->Frame());
      } else {
        // HidePopup will take care of hiding any of its children, so
        // break out afterwards
        HidePopup(child->Element(), {HidePopupOption::Async});
        break;
      }
    }
  }

  RemoveMenuChainItem(item);
  HidePopupsInList(popupsToHide);
}

bool nsXULPopupManager::HasContextMenu(nsMenuPopupFrame* aPopup) {
  nsMenuChainItem* item = GetTopVisibleMenu();
  while (item && item->Frame() != aPopup) {
    if (item->IsContextMenu()) {
      return true;
    }
    item = item->GetParent();
  }

  return false;
}

void nsXULPopupManager::SetCaptureState(nsIContent* aOldPopup) {
  nsMenuChainItem* item = GetTopVisibleMenu();
  if (item && aOldPopup == item->Element()) {
    return;
  }

  if (mWidget) {
    mWidget->CaptureRollupEvents(false);
    mWidget = nullptr;
  }

  if (item) {
    nsMenuPopupFrame* popup = item->Frame();
    mWidget = popup->GetWidget();
    if (mWidget) {
      mWidget->CaptureRollupEvents(true);
    }
  }

  UpdateKeyboardListeners();
}

void nsXULPopupManager::UpdateKeyboardListeners() {
  nsCOMPtr<EventTarget> newTarget;
  bool isForMenu = false;
  if (nsMenuChainItem* item = GetTopVisibleMenu()) {
    if (item->IgnoreKeys() != eIgnoreKeys_True) {
      newTarget = item->Element()->GetComposedDoc();
    }
    isForMenu = item->GetPopupType() == PopupType::Menu;
  } else if (mActiveMenuBar && mActiveMenuBar->IsActiveByKeyboard()) {
    // Only listen for key events iff menubar is activated via key, see
    // bug 1818241.
    newTarget = mActiveMenuBar->GetComposedDoc();
    isForMenu = true;
  }

  if (mKeyListener != newTarget) {
    OwningNonNull<nsXULPopupManager> kungFuDeathGrip(*this);
    if (mKeyListener) {
      mKeyListener->RemoveEventListener(u"keypress"_ns, this, true);
      mKeyListener->RemoveEventListener(u"keydown"_ns, this, true);
      mKeyListener->RemoveEventListener(u"keyup"_ns, this, true);
      mKeyListener = nullptr;
      nsContentUtils::NotifyInstalledMenuKeyboardListener(false);
    }

    if (newTarget) {
      newTarget->AddEventListener(u"keypress"_ns, this, true);
      newTarget->AddEventListener(u"keydown"_ns, this, true);
      newTarget->AddEventListener(u"keyup"_ns, this, true);
      nsContentUtils::NotifyInstalledMenuKeyboardListener(isForMenu);
      mKeyListener = std::move(newTarget);
    }
  }
}

void nsXULPopupManager::UpdateMenuItems(Element* aPopup) {
  // Walk all of the menu's children, checking to see if any of them has a
  // command attribute. If so, then several attributes must potentially be
  // updated.

  nsCOMPtr<Document> document = aPopup->GetUncomposedDoc();
  if (!document) {
    return;
  }

  // When a menu is opened, make sure that command updating is unlocked first.
  nsCOMPtr<nsIDOMXULCommandDispatcher> commandDispatcher =
      document->GetCommandDispatcher();
  if (commandDispatcher) {
    commandDispatcher->Unlock();
  }

  for (nsCOMPtr<nsIContent> grandChild = aPopup->GetFirstChild(); grandChild;
       grandChild = grandChild->GetNextSibling()) {
    if (grandChild->IsXULElement(nsGkAtoms::menugroup)) {
      if (grandChild->GetChildCount() == 0) {
        continue;
      }
      grandChild = grandChild->GetFirstChild();
    }
    if (grandChild->IsXULElement(nsGkAtoms::menuitem)) {
      // See if we have a command attribute.
      Element* grandChildElement = grandChild->AsElement();
      nsAutoString command;
      grandChildElement->GetAttr(nsGkAtoms::command, command);
      if (!command.IsEmpty()) {
        // We do! Look it up in our document
        RefPtr<dom::Element> commandElement = document->GetElementById(command);
        if (commandElement) {
          // The menu's disabled state needs to be updated to match the command.
          grandChildElement->SetBoolAttr(
              nsGkAtoms::disabled,
              commandElement->GetBoolAttr(nsGkAtoms::disabled));

          // The menu's label and accesskey states need to be updated to match
          // the command. Note that unlike the disabled state if the command has
          // *no* value, we assume the menu is supplying its own.
          nsAutoString commandValue;
          if (commandElement->GetAttr(nsGkAtoms::label, commandValue)) {
            grandChildElement->SetAttr(kNameSpaceID_None, nsGkAtoms::label,
                                       commandValue, true);
          }

          if (commandElement->GetAttr(nsGkAtoms::accesskey, commandValue)) {
            grandChildElement->SetAttr(kNameSpaceID_None, nsGkAtoms::accesskey,
                                       commandValue, true);
          }
        }
      }
    }
    if (!grandChild->GetNextSibling() &&
        grandChild->GetParent()->IsXULElement(nsGkAtoms::menugroup)) {
      grandChild = grandChild->GetParent();
    }
  }
}

// Notify
//
// The item selection timer has fired, we might have to readjust the
// selected item. There are two cases here that we are trying to deal with:
//   (1) diagonal movement from a parent menu to a submenu passing briefly over
//       other items, and
//   (2) moving out from a submenu to a parent or grandparent menu.
// In both cases, |mTimerMenu| is the menu item that might have an open submenu
// and the first item in |mPopups| is the item the mouse is currently over,
// which could be none of them.
//
// case (1):
//  As the mouse moves from the parent item of a submenu (we'll call 'A')
//  diagonally into the submenu, it probably passes through one or more
//  sibilings (B). As the mouse passes through B, it becomes the current menu
//  item and the timer is set and mTimerMenu is set to A. Before the timer
//  fires, the mouse leaves the menu containing A and B and enters the submenus.
//  Now when the timer fires, |mPopups| is null (!= |mTimerMenu|) so we have to
//  see if anything in A's children is selected (recall that even disabled items
//  are selected, the style just doesn't show it). If that is the case, we need
//  to set the selected item back to A.
//
// case (2);
//  Item A has an open submenu, and in it there is an item (B) which also has an
//  open submenu (so there are 3 menus displayed right now). The mouse then
//  leaves B's child submenu and selects an item that is a sibling of A, call it
//  C. When the mouse enters C, the timer is set and |mTimerMenu| is A and
//  |mPopups| is C. As the timer fires, the mouse is still within C. The correct
//  behavior is to set the current item to C and close up the chain parented at
//  A.
//
//  This brings up the question of is the logic of case (1) enough? The answer
//  is no, and is discussed in bugzilla bug 29400. Case (1) asks if A's submenu
//  has a selected child, and if it does, set the selected item to A. Because B
//  has a submenu open, it is selected and as a result, A is set to be the
//  selected item even though the mouse rests in C -- very wrong.
//
//  The solution is to use the same idea, but instead of only checking one
//  level, drill all the way down to the deepest open submenu and check if it
//  has something selected. Since the mouse is in a grandparent, it won't, and
//  we know that we can safely close up A and all its children.
//
// The code below melds the two cases together.
//
void nsXULPopupManager::KillMenuTimer() {
  if (mCloseTimer && mTimerMenu) {
    mCloseTimer->Cancel();
    mCloseTimer = nullptr;

    if (mTimerMenu->IsOpen()) {
      HidePopup(&mTimerMenu->PopupElement(), {HidePopupOption::Async});
    }
  }

  mTimerMenu = nullptr;
}

void nsXULPopupManager::CancelMenuTimer(nsMenuPopupFrame* aMenu) {
  if (mCloseTimer && mTimerMenu == aMenu) {
    mCloseTimer->Cancel();
    mCloseTimer = nullptr;
    mTimerMenu = nullptr;
  }
}

bool nsXULPopupManager::HandleShortcutNavigation(KeyboardEvent& aKeyEvent,
                                                 nsMenuPopupFrame* aFrame) {
  // On Windows, don't check shortcuts when the accelerator key is down.
#ifdef XP_WIN
  WidgetInputEvent* evt = aKeyEvent.WidgetEventPtr()->AsInputEvent();
  if (evt && evt->IsAccel()) {
    return false;
  }
#endif

  if (!aFrame) {
    if (nsMenuChainItem* item = GetTopVisibleMenu()) {
      aFrame = item->Frame();
    }
  }

  if (aFrame) {
    bool action = false;
    RefPtr result = aFrame->FindMenuWithShortcut(aKeyEvent, action);
    if (!result) {
      return false;
    }
    RefPtr popup = &aFrame->PopupElement();
    popup->SetActiveMenuChild(result, XULMenuParentElement::ByKey::Yes);
    if (action) {
      WidgetEvent* evt = aKeyEvent.WidgetEventPtr();
      result->HandleEnterKeyPress(*evt);
    }
    return true;
  }

  // Only do shortcut navigation when the menubar is activated via keyboard.
  if (mActiveMenuBar) {
    RefPtr menubar = mActiveMenuBar;
    if (RefPtr result = menubar->FindMenuWithShortcut(aKeyEvent)) {
      result->OpenMenuPopup(true);
      return true;
    }
#ifdef XP_WIN
    // Behavior on Windows - this item is on the menu bar, beep and deactivate
    // the menu bar.
    // TODO(emilio): This is rather odd, and I cannot get the beep to work,
    // but this matches what old code was doing...
    if (nsCOMPtr<nsISound> sound = do_GetService("@mozilla.org/sound;1")) {
      sound->Beep();
    }
    menubar->SetActive(false);
#endif
  }
  return false;
}

bool nsXULPopupManager::HandleKeyboardNavigation(uint32_t aKeyCode) {
  if (nsMenuChainItem* nextitem = GetTopVisibleMenu()) {
    nextitem->Element()->OwnerDoc()->FlushPendingNotifications(
        FlushType::Frames);
  }

  // navigate up through the open menus, looking for the topmost one
  // in the same hierarchy
  nsMenuChainItem* item = nullptr;
  nsMenuChainItem* nextitem = GetTopVisibleMenu();
  while (nextitem) {
    item = nextitem;
    nextitem = item->GetParent();

    if (!nextitem) {
      break;
    }
    // stop if the parent isn't a menu
    if (!nextitem->IsMenu()) {
      break;
    }

    // Check to make sure that the parent is actually the parent menu. It won't
    // be if the parent is in a different frame hierarchy, for example, for a
    // context menu opened on another menu.
    XULPopupElement& expectedParent = nextitem->Frame()->PopupElement();
    auto* menu = item->Frame()->PopupElement().GetContainingMenu();
    if (!menu || menu->GetMenuParent() != &expectedParent) {
      break;
    }
  }

  nsIFrame* itemFrame;
  if (item) {
    itemFrame = item->Frame();
  } else if (mActiveMenuBar) {
    itemFrame = mActiveMenuBar->GetPrimaryFrame();
    if (!itemFrame) {
      return false;
    }
  } else {
    return false;
  }

  nsNavigationDirection theDirection;
  NS_ASSERTION(aKeyCode >= KeyboardEvent_Binding::DOM_VK_END &&
                   aKeyCode <= KeyboardEvent_Binding::DOM_VK_DOWN,
               "Illegal key code");
  theDirection = NS_DIRECTION_FROM_KEY_CODE(itemFrame, aKeyCode);

  bool selectFirstItem = true;
#ifdef MOZ_WIDGET_GTK
  {
    XULButtonElement* currentItem = nullptr;
    if (item && mActiveMenuBar && NS_DIRECTION_IS_INLINE(theDirection)) {
      currentItem = item->Frame()->PopupElement().GetActiveMenuChild();
      // If nothing is selected in the menu and we have a menubar, let it
      // handle the movement not to steal focus from it.
      if (!currentItem) {
        item = nullptr;
      }
    }
    // On menu change, only select first item if an item is already selected.
    selectFirstItem = !!currentItem;
  }
#endif

  // if a popup is open, first check for navigation within the popup
  if (item && HandleKeyboardNavigationInPopup(item, theDirection)) {
    return true;
  }

  // no popup handled the key, so check the active menubar, if any
  if (!mActiveMenuBar) {
    return false;
  }
  RefPtr menubar = mActiveMenuBar;
  if (NS_DIRECTION_IS_INLINE(theDirection)) {
    RefPtr prevActiveItem = menubar->GetActiveMenuChild();
    const bool open = prevActiveItem && prevActiveItem->IsMenuPopupOpen();
    RefPtr nextItem = theDirection == eNavigationDirection_End
                          ? menubar->GetNextMenuItem()
                          : menubar->GetPrevMenuItem();
    menubar->SetActiveMenuChild(nextItem, XULMenuParentElement::ByKey::Yes);
    if (open && nextItem) {
      nextItem->OpenMenuPopup(selectFirstItem);
    }
    return true;
  }
  if (NS_DIRECTION_IS_BLOCK(theDirection)) {
    // Open the menu and select its first item.
    if (RefPtr currentMenu = menubar->GetActiveMenuChild()) {
      ShowMenu(currentMenu, selectFirstItem);
    }
    return true;
  }
  return false;
}

bool nsXULPopupManager::HandleKeyboardNavigationInPopup(
    nsMenuChainItem* item, nsMenuPopupFrame* aFrame,
    nsNavigationDirection aDir) {
  NS_ASSERTION(aFrame, "aFrame is null");
  NS_ASSERTION(!item || item->Frame() == aFrame,
               "aFrame is expected to be equal to item->Frame()");

  using Wrap = XULMenuParentElement::Wrap;
  RefPtr<XULPopupElement> menu = &aFrame->PopupElement();

  aFrame->ClearIncrementalString();
  RefPtr currentItem = aFrame->GetCurrentMenuItem();

  // This method only gets called if we're open.
  if (!currentItem && NS_DIRECTION_IS_INLINE(aDir)) {
    // We've been opened, but we haven't had anything selected.
    // We can handle End, but our parent handles Start.
    if (aDir == eNavigationDirection_End) {
      if (RefPtr nextItem = menu->GetNextMenuItem(Wrap::No)) {
        menu->SetActiveMenuChild(nextItem, XULMenuParentElement::ByKey::Yes);
        return true;
      }
    }
    return false;
  }

  const bool isContainer = currentItem && !currentItem->IsMenuItem();
  const bool isOpen = currentItem && currentItem->IsMenuPopupOpen();
  if (isOpen) {
    // For an open popup, have the child process the event
    nsMenuChainItem* child = item ? item->GetChild() : nullptr;
    if (child && HandleKeyboardNavigationInPopup(child, aDir)) {
      return true;
    }
  } else if (aDir == eNavigationDirection_End && isContainer &&
             !currentItem->IsDisabled()) {
    currentItem->OpenMenuPopup(true);
    return true;
  }

  // For block progression, we can move in either direction
  if (NS_DIRECTION_IS_BLOCK(aDir) || NS_DIRECTION_IS_BLOCK_TO_EDGE(aDir)) {
    RefPtr<XULButtonElement> nextItem = nullptr;

    if (aDir == eNavigationDirection_Before ||
        aDir == eNavigationDirection_After) {
      // Cursor navigation does not wrap on Mac or for menulists on Windows.
      auto wrap =
#ifdef XP_WIN
          aFrame->IsMenuList() ? Wrap::No : Wrap::Yes;
#elif defined XP_MACOSX
          Wrap::No;
#else
          Wrap::Yes;
#endif

      if (aDir == eNavigationDirection_Before) {
        nextItem = menu->GetPrevMenuItem(wrap);
      } else {
        nextItem = menu->GetNextMenuItem(wrap);
      }
    } else if (aDir == eNavigationDirection_First) {
      nextItem = menu->GetFirstMenuItem();
    } else {
      nextItem = menu->GetLastMenuItem();
    }

    if (nextItem) {
      menu->SetActiveMenuChild(nextItem, XULMenuParentElement::ByKey::Yes);
      return true;
    }
  } else if (currentItem && isOpen && aDir == eNavigationDirection_Start) {
    // close a submenu when Left is pressed
    if (nsMenuPopupFrame* popupFrame =
            currentItem->GetMenuPopup(FlushType::None)) {
      HidePopup(&popupFrame->PopupElement(), {});
    }
    return true;
  }

  return false;
}

bool nsXULPopupManager::HandleKeyboardEventWithKeyCode(
    KeyboardEvent* aKeyEvent, nsMenuChainItem* aTopVisibleMenuItem) {
  uint32_t keyCode = aKeyEvent->KeyCode();

  // Escape should close panels, but the other keys should have no effect.
  if (aTopVisibleMenuItem &&
      aTopVisibleMenuItem->GetPopupType() != PopupType::Menu) {
    if (keyCode == KeyboardEvent_Binding::DOM_VK_ESCAPE) {
      HidePopup(aTopVisibleMenuItem->Element(), {HidePopupOption::IsRollup});
      aKeyEvent->StopPropagation();
      aKeyEvent->StopCrossProcessForwarding();
      aKeyEvent->PreventDefault();
    }
    return true;
  }

  bool consume = (aTopVisibleMenuItem || mActiveMenuBar);
  switch (keyCode) {
    case KeyboardEvent_Binding::DOM_VK_UP:
    case KeyboardEvent_Binding::DOM_VK_DOWN:
#ifndef XP_MACOSX
      // roll up the popup when alt+up/down are pressed within a menulist.
      if (aKeyEvent->AltKey() && aTopVisibleMenuItem &&
          aTopVisibleMenuItem->Frame()->IsMenuList()) {
        Rollup({});
        break;
      }
      [[fallthrough]];
#endif

    case KeyboardEvent_Binding::DOM_VK_LEFT:
    case KeyboardEvent_Binding::DOM_VK_RIGHT:
    case KeyboardEvent_Binding::DOM_VK_HOME:
    case KeyboardEvent_Binding::DOM_VK_END:
      HandleKeyboardNavigation(keyCode);
      break;

    case KeyboardEvent_Binding::DOM_VK_PAGE_DOWN:
    case KeyboardEvent_Binding::DOM_VK_PAGE_UP:
      if (aTopVisibleMenuItem) {
        aTopVisibleMenuItem->Frame()->ChangeByPage(
            keyCode == KeyboardEvent_Binding::DOM_VK_PAGE_UP);
      }
      break;

    case KeyboardEvent_Binding::DOM_VK_ESCAPE:
      // Pressing Escape hides one level of menus only. If no menu is open,
      // check if a menubar is active and inform it that a menu closed. Even
      // though in this latter case, a menu didn't actually close, the effect
      // ends up being the same. Similar for the tab key below.
      if (aTopVisibleMenuItem) {
        HidePopup(aTopVisibleMenuItem->Element(), {HidePopupOption::IsRollup});
      } else if (mActiveMenuBar) {
        RefPtr menubar = mActiveMenuBar;
        menubar->SetActive(false);
      }
      break;

    case KeyboardEvent_Binding::DOM_VK_TAB:
#ifndef XP_MACOSX
    case KeyboardEvent_Binding::DOM_VK_F10:
#endif
      if (aTopVisibleMenuItem &&
          !aTopVisibleMenuItem->Frame()->PopupElement().AttrValueIs(
              kNameSpaceID_None, nsGkAtoms::activateontab, nsGkAtoms::_true,
              eCaseMatters)) {
        // Close popups or deactivate menubar when Tab or F10 are pressed
        Rollup({});
        break;
      } else if (mActiveMenuBar) {
        RefPtr menubar = mActiveMenuBar;
        menubar->SetActive(false);
        break;
      }
      // Intentional fall-through to RETURN case
      [[fallthrough]];

    case KeyboardEvent_Binding::DOM_VK_RETURN: {
      // If there is a popup open, check if the current item needs to be opened.
      // Otherwise, tell the active menubar, if any, to activate the menu. The
      // Enter method will return a menu if one needs to be opened as a result.
      WidgetEvent* event = aKeyEvent->WidgetEventPtr();
      if (aTopVisibleMenuItem) {
        aTopVisibleMenuItem->Frame()->HandleEnterKeyPress(*event);
      } else if (mActiveMenuBar) {
        RefPtr menubar = mActiveMenuBar;
        menubar->HandleEnterKeyPress(*event);
      }
      break;
    }

    default:
      return false;
  }

  if (consume) {
    aKeyEvent->StopPropagation();
    aKeyEvent->StopCrossProcessForwarding();
    aKeyEvent->PreventDefault();
  }
  return true;
}

nsresult nsXULPopupManager::HandleEvent(Event* aEvent) {
  RefPtr<KeyboardEvent> keyEvent = aEvent->AsKeyboardEvent();
  NS_ENSURE_TRUE(keyEvent, NS_ERROR_UNEXPECTED);

  // handlers shouldn't be triggered by non-trusted events.
  if (!keyEvent->IsTrusted()) {
    return NS_OK;
  }

  nsAutoString eventType;
  keyEvent->GetType(eventType);
  if (eventType.EqualsLiteral("keyup")) {
    return KeyUp(keyEvent);
  }
  if (eventType.EqualsLiteral("keydown")) {
    return KeyDown(keyEvent);
  }
  if (eventType.EqualsLiteral("keypress")) {
    return KeyPress(keyEvent);
  }

  MOZ_ASSERT_UNREACHABLE("Unexpected eventType");
  return NS_OK;
}

nsresult nsXULPopupManager::UpdateIgnoreKeys(bool aIgnoreKeys) {
  nsMenuChainItem* item = GetTopVisibleMenu();
  if (item) {
    item->SetIgnoreKeys(aIgnoreKeys ? eIgnoreKeys_True : eIgnoreKeys_Shortcuts);
  }
  UpdateKeyboardListeners();
  return NS_OK;
}

nsPopupState nsXULPopupManager::GetPopupState(Element* aPopupElement) {
  if (mNativeMenu && mNativeMenu->Element()->Contains(aPopupElement)) {
    if (aPopupElement != mNativeMenu->Element()) {
      // Submenu state is stored in mNativeMenuSubmenuStates.
      return mNativeMenuSubmenuStates.MaybeGet(aPopupElement)
          .valueOr(ePopupClosed);
    }
    // mNativeMenu->Element()'s state is stored in its nsMenuPopupFrame.
  }

  nsMenuPopupFrame* menuPopupFrame =
      do_QueryFrame(aPopupElement->GetPrimaryFrame());
  if (menuPopupFrame) {
    return menuPopupFrame->PopupState();
  }
  return ePopupClosed;
}

nsresult nsXULPopupManager::KeyUp(KeyboardEvent* aKeyEvent) {
  // don't do anything if a menu isn't open or a menubar isn't active
  if (!mActiveMenuBar) {
    nsMenuChainItem* item = GetTopVisibleMenu();
    if (!item || item->GetPopupType() != PopupType::Menu) {
      return NS_OK;
    }

    if (item->IgnoreKeys() == eIgnoreKeys_Shortcuts) {
      aKeyEvent->StopCrossProcessForwarding();
      return NS_OK;
    }
  }

  aKeyEvent->StopPropagation();
  aKeyEvent->StopCrossProcessForwarding();
  aKeyEvent->PreventDefault();

  return NS_OK;  // I am consuming event
}

nsresult nsXULPopupManager::KeyDown(KeyboardEvent* aKeyEvent) {
  nsMenuChainItem* item = GetTopVisibleMenu();
  if (item && item->Frame()->PopupElement().IsLocked()) {
    return NS_OK;
  }

  if (HandleKeyboardEventWithKeyCode(aKeyEvent, item)) {
    return NS_OK;
  }

  // don't do anything if a menu isn't open or a menubar isn't active
  if (!mActiveMenuBar && (!item || item->GetPopupType() != PopupType::Menu)) {
    return NS_OK;
  }

  // Since a menu was open, stop propagation of the event to keep other event
  // listeners from becoming confused.
  if (!item || item->IgnoreKeys() != eIgnoreKeys_Shortcuts) {
    aKeyEvent->StopPropagation();
  }

  // If the key just pressed is the access key (usually Alt),
  // dismiss and unfocus the menu.
  uint32_t menuAccessKey = LookAndFeel::GetMenuAccessKey();
  if (menuAccessKey) {
    uint32_t theChar = aKeyEvent->KeyCode();

    if (theChar == menuAccessKey) {
      bool ctrl = (menuAccessKey != KeyboardEvent_Binding::DOM_VK_CONTROL &&
                   aKeyEvent->CtrlKey());
      bool alt = (menuAccessKey != KeyboardEvent_Binding::DOM_VK_ALT &&
                  aKeyEvent->AltKey());
      bool shift = (menuAccessKey != KeyboardEvent_Binding::DOM_VK_SHIFT &&
                    aKeyEvent->ShiftKey());
      bool meta = (menuAccessKey != KeyboardEvent_Binding::DOM_VK_META &&
                   aKeyEvent->MetaKey());
      if (!(ctrl || alt || shift || meta)) {
        // The access key just went down and no other
        // modifiers are already down.
        nsMenuChainItem* item = GetTopVisibleMenu();
        if (item && !item->Frame()->IsMenuList()) {
          Rollup({});
        } else if (mActiveMenuBar) {
          RefPtr menubar = mActiveMenuBar;
          menubar->SetActive(false);
        }

        // Clear the item to avoid bugs as it may have been deleted during
        // rollup.
        item = nullptr;
      }
      aKeyEvent->StopPropagation();
      aKeyEvent->PreventDefault();
    }
  }

  aKeyEvent->StopCrossProcessForwarding();
  return NS_OK;
}

nsresult nsXULPopupManager::KeyPress(KeyboardEvent* aKeyEvent) {
  // Don't check prevent default flag -- menus always get first shot at key
  // events.

  nsMenuChainItem* item = GetTopVisibleMenu();
  if (item && (item->Frame()->PopupElement().IsLocked() ||
               item->GetPopupType() != PopupType::Menu)) {
    return NS_OK;
  }

  // if a menu is open or a menubar is active, it consumes the key event
  bool consume = (item || mActiveMenuBar);

  WidgetInputEvent* evt = aKeyEvent->WidgetEventPtr()->AsInputEvent();
  bool isAccel = evt && evt->IsAccel();

  // When ignorekeys="shortcuts" is used, we don't call preventDefault on the
  // key event when the accelerator key is pressed. This allows another
  // listener to handle keys. For instance, this allows global shortcuts to
  // still apply while a menu is open.
  if (item && item->IgnoreKeys() == eIgnoreKeys_Shortcuts && isAccel) {
    consume = false;
  }

  HandleShortcutNavigation(*aKeyEvent, nullptr);

  aKeyEvent->StopCrossProcessForwarding();
  if (consume) {
    aKeyEvent->StopPropagation();
    aKeyEvent->PreventDefault();
  }

  return NS_OK;  // I am consuming event
}

NS_IMETHODIMP
nsXULPopupHidingEvent::Run() {
  RefPtr<nsXULPopupManager> pm = nsXULPopupManager::GetInstance();
  Document* document = mPopup->GetUncomposedDoc();
  if (pm && document) {
    if (RefPtr<nsPresContext> presContext = document->GetPresContext()) {
      nsCOMPtr<Element> popup = mPopup;
      nsCOMPtr<Element> nextPopup = mNextPopup;
      nsCOMPtr<Element> lastPopup = mLastPopup;
      pm->FirePopupHidingEvent(popup, nextPopup, lastPopup, presContext,
                               mPopupType, mOptions);
    }
  }
  return NS_OK;
}

bool nsXULPopupPositionedEvent::DispatchIfNeeded(Element* aPopup) {
  // The popuppositioned event only fires on arrow panels for now.
  if (aPopup->AttrValueIs(kNameSpaceID_None, nsGkAtoms::type, nsGkAtoms::arrow,
                          eCaseMatters)) {
    nsCOMPtr<nsIRunnable> event =
        MakeAndAddRef<nsXULPopupPositionedEvent>(aPopup);
    aPopup->OwnerDoc()->Dispatch(event.forget());
    return true;
  }

  return false;
}

static void AlignmentPositionToString(nsMenuPopupFrame* aFrame,
                                      nsAString& aString) {
  aString.Truncate();
  int8_t position = aFrame->GetAlignmentPosition();
  switch (position) {
    case POPUPPOSITION_AFTERSTART:
      return aString.AssignLiteral("after_start");
    case POPUPPOSITION_AFTEREND:
      return aString.AssignLiteral("after_end");
    case POPUPPOSITION_BEFORESTART:
      return aString.AssignLiteral("before_start");
    case POPUPPOSITION_BEFOREEND:
      return aString.AssignLiteral("before_end");
    case POPUPPOSITION_STARTBEFORE:
      return aString.AssignLiteral("start_before");
    case POPUPPOSITION_ENDBEFORE:
      return aString.AssignLiteral("end_before");
    case POPUPPOSITION_STARTAFTER:
      return aString.AssignLiteral("start_after");
    case POPUPPOSITION_ENDAFTER:
      return aString.AssignLiteral("end_after");
    case POPUPPOSITION_OVERLAP:
      return aString.AssignLiteral("overlap");
    case POPUPPOSITION_AFTERPOINTER:
      return aString.AssignLiteral("after_pointer");
    case POPUPPOSITION_SELECTION:
      return aString.AssignLiteral("selection");
    default:
      // Leave as an empty string.
      break;
  }
}

static void PopupAlignmentToString(nsMenuPopupFrame* aFrame,
                                   nsAString& aString) {
  aString.Truncate();
  int alignment = aFrame->GetPopupAlignment();
  switch (alignment) {
    case POPUPALIGNMENT_TOPLEFT:
      return aString.AssignLiteral("topleft");
    case POPUPALIGNMENT_TOPRIGHT:
      return aString.AssignLiteral("topright");
    case POPUPALIGNMENT_BOTTOMLEFT:
      return aString.AssignLiteral("bottomleft");
    case POPUPALIGNMENT_BOTTOMRIGHT:
      return aString.AssignLiteral("bottomright");
    case POPUPALIGNMENT_LEFTCENTER:
      return aString.AssignLiteral("leftcenter");
    case POPUPALIGNMENT_RIGHTCENTER:
      return aString.AssignLiteral("rightcenter");
    case POPUPALIGNMENT_TOPCENTER:
      return aString.AssignLiteral("topcenter");
    case POPUPALIGNMENT_BOTTOMCENTER:
      return aString.AssignLiteral("bottomcenter");
    default:
      // Leave as an empty string.
      break;
  }
}

NS_IMETHODIMP
MOZ_CAN_RUN_SCRIPT_BOUNDARY
nsXULPopupPositionedEvent::Run() {
  RefPtr<nsXULPopupManager> pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return NS_OK;
  }
  nsMenuPopupFrame* popupFrame = do_QueryFrame(mPopup->GetPrimaryFrame());
  if (!popupFrame) {
    return NS_OK;
  }

  popupFrame->WillDispatchPopupPositioned();

  // At this point, hidePopup may have been called but it currently has no
  // way to stop this event. However, if hidePopup was called, the popup
  // will now be in the hiding or closed state. If we are in the shown or
  // positioning state instead, we can assume that we are still clear to
  // open/move the popup
  nsPopupState state = popupFrame->PopupState();
  if (state != ePopupPositioning && state != ePopupShown) {
    return NS_OK;
  }

  // Note that the offset might be along either the X or Y axis, but for the
  // sake of simplicity we use a point with only the X axis set so we can
  // use ToNearestPixels().
  int32_t popupOffset = nsPoint(popupFrame->GetAlignmentOffset(), 0)
                            .ToNearestPixels(AppUnitsPerCSSPixel())
                            .x;

  PopupPositionedEventInit init;
  init.mComposed = true;
  init.mIsAnchored = popupFrame->IsAnchored();
  init.mAlignmentOffset = popupOffset;
  AlignmentPositionToString(popupFrame, init.mAlignmentPosition);
  PopupAlignmentToString(popupFrame, init.mPopupAlignment);
  RefPtr<PopupPositionedEvent> event =
      PopupPositionedEvent::Constructor(mPopup, u"popuppositioned"_ns, init);
  event->SetTrusted(true);

  mPopup->DispatchEvent(*event);

  // Get the popup frame and make sure it is still in the positioning
  // state. If it isn't, someone may have tried to reshow or hide it
  // during the popuppositioned event.
  // Alternately, this event may have been fired in reponse to moving the
  // popup rather than opening it. In that case, we are done.
  popupFrame = do_QueryFrame(mPopup->GetPrimaryFrame());
  if (popupFrame && popupFrame->PopupState() == ePopupPositioning) {
    pm->ShowPopupCallback(mPopup, popupFrame, false, false);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsXULMenuCommandEvent::Run() {
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return NS_OK;
  }

  RefPtr menu = XULButtonElement::FromNode(mMenu);
  MOZ_ASSERT(menu);
  if (mFlipChecked) {
    menu->SetBoolAttr(nsGkAtoms::checked,
                      !menu->GetBoolAttr(nsGkAtoms::checked));
  }

  RefPtr<nsPresContext> presContext = menu->OwnerDoc()->GetPresContext();
  RefPtr<PresShell> presShell =
      presContext ? presContext->PresShell() : nullptr;

  // Deselect ourselves.
  if (mCloseMenuMode != CloseMenuMode_None) {
    if (RefPtr parent = menu->GetMenuParent()) {
      if (parent->GetActiveMenuChild() == menu) {
        parent->SetActiveMenuChild(nullptr);
      }
    }
  }

  AutoHandlingUserInputStatePusher userInpStatePusher(mUserInput);
  nsContentUtils::DispatchXULCommand(
      menu, mIsTrusted, nullptr, presShell, mModifiers & MODIFIER_CONTROL,
      mModifiers & MODIFIER_ALT, mModifiers & MODIFIER_SHIFT,
      mModifiers & MODIFIER_META, 0, mButton);

  if (mCloseMenuMode != CloseMenuMode_None) {
    if (RefPtr popup = menu->GetContainingPopupElement()) {
      HidePopupOptions options{HidePopupOption::DeselectMenu};
      if (mCloseMenuMode == CloseMenuMode_Auto) {
        options += HidePopupOption::HideChain;
      }
      pm->HidePopup(popup, options);
    }
  }

  return NS_OK;
}
