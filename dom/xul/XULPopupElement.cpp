/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/XULPopupElement.h"

#include "XULMenuParentElement.h"
#include "mozilla/AppUnits.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/XULButtonElement.h"
#include "mozilla/dom/XULMenuElement.h"
#include "mozilla/dom/XULPopupElementBinding.h"
#include "nsCOMPtr.h"
#include "nsDOMCSSDeclaration.h"
#include "nsIContent.h"
#include "nsMenuPopupFrame.h"
#include "nsNameSpaceManager.h"
#include "nsStringFwd.h"
#ifdef MOZ_WAYLAND
#  include "mozilla/WidgetUtilsGtk.h"
#endif

namespace mozilla::dom {

nsXULElement* NS_NewXULPopupElement(
    already_AddRefed<mozilla::dom::NodeInfo>&& aNodeInfo) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo(aNodeInfo);
  auto* nim = nodeInfo->NodeInfoManager();
  return new (nim) XULPopupElement(nodeInfo.forget());
}

JSObject* XULPopupElement::WrapNode(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto) {
  return XULPopupElement_Binding::Wrap(aCx, this, aGivenProto);
}

nsMenuPopupFrame* XULPopupElement::GetFrame(FlushType aFlushType) {
  nsIFrame* f = GetPrimaryFrame(aFlushType);
  MOZ_ASSERT(!f || f->IsMenuPopupFrame());
  return static_cast<nsMenuPopupFrame*>(f);
}

void XULPopupElement::OpenPopup(Element* aAnchorElement,
                                const StringOrOpenPopupOptions& aOptions,
                                int32_t aXPos, int32_t aYPos,
                                bool aIsContextMenu, bool aAttributesOverride,
                                Event* aTriggerEvent) {
  nsAutoString position;
  if (aOptions.IsOpenPopupOptions()) {
    const OpenPopupOptions& options = aOptions.GetAsOpenPopupOptions();
    position = options.mPosition;
    aXPos = options.mX;
    aYPos = options.mY;
    aIsContextMenu = options.mIsContextMenu;
    aAttributesOverride = options.mAttributesOverride;
    aTriggerEvent = options.mTriggerEvent;
  } else {
    position = aOptions.GetAsString();
  }

  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return;
  }
  // As a special case for popups that are menus when no anchor or position
  // are specified, open the popup with ShowMenu instead of ShowPopup so that
  // the popup is aligned with the menu.
  if (!aAnchorElement && position.IsEmpty() && GetPrimaryFrame()) {
    if (auto* menu = GetContainingMenu()) {
      pm->ShowMenu(menu, false);
      return;
    }
  }
  pm->ShowPopup(this, aAnchorElement, position, aXPos, aYPos, aIsContextMenu,
                aAttributesOverride, false, aTriggerEvent);
}

void XULPopupElement::OpenPopupAtScreen(int32_t aXPos, int32_t aYPos,
                                        bool aIsContextMenu,
                                        Event* aTriggerEvent) {
  if (NodeInfo()->NameAtom() == nsGkAtoms::panel) {
    // TODO(bug 2038354): Remove this and make the front-end set the attribute
    // explicitly.
    SetAttr(kNameSpaceID_None, nsGkAtoms::nonnative, u"true"_ns, true);
  }
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (pm) {
    pm->ShowPopupAtScreen(this, aXPos, aYPos, aIsContextMenu, aTriggerEvent);
  }
}

void XULPopupElement::OpenPopupAtScreenRect(const nsAString& aPosition,
                                            int32_t aXPos, int32_t aYPos,
                                            int32_t aWidth, int32_t aHeight,
                                            bool aIsContextMenu,
                                            bool aAttributesOverride,
                                            Event* aTriggerEvent) {
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (NodeInfo()->NameAtom() == nsGkAtoms::panel) {
    // TODO(bug 2038354): Remove this and make the front-end set the attribute
    // explicitly.
    SetAttr(kNameSpaceID_None, nsGkAtoms::nonnative, u"true"_ns, true);
  }
  if (pm) {
    pm->ShowPopupAtScreenRect(
        this, aPosition, nsIntRect(aXPos, aYPos, aWidth, aHeight),
        aIsContextMenu, aAttributesOverride, aTriggerEvent);
  }
}

void XULPopupElement::HidePopup(bool aCancel) {
  nsXULPopupManager* pm = nsXULPopupManager::GetInstance();
  if (!pm) {
    return;
  }
  HidePopupOptions options{HidePopupOption::DeselectMenu};
  if (aCancel) {
    options += HidePopupOption::IsRollup;
  }
  pm->HidePopup(this, options);
}

static Modifiers ConvertModifiers(const ActivateMenuItemOptions& aModifiers) {
  Modifiers modifiers = 0;
  if (aModifiers.mCtrlKey) {
    modifiers |= MODIFIER_CONTROL;
  }
  if (aModifiers.mAltKey) {
    modifiers |= MODIFIER_ALT;
  }
  if (aModifiers.mShiftKey) {
    modifiers |= MODIFIER_SHIFT;
  }
  if (aModifiers.mMetaKey) {
    modifiers |= MODIFIER_META;
  }
  return modifiers;
}

void XULPopupElement::PopupOpened(bool aSelectFirstItem) {
  if (aSelectFirstItem) {
    SelectFirstItem();
  }
  if (RefPtr button = GetContainingMenu()) {
    if (RefPtr parent = button->GetMenuParent()) {
      parent->SetActiveMenuChild(button);
    }
  }
}

void XULPopupElement::PopupClosed(bool aDeselectMenu) {
  LockMenuUntilClosed(false);
  SetActiveMenuChild(nullptr);
  auto dispatcher = MakeRefPtr<AsyncEventDispatcher>(
      this, u"DOMMenuInactive"_ns, CanBubble::eYes, ChromeOnlyDispatch::eNo);
  dispatcher->PostDOMEvent();
  if (RefPtr button = GetContainingMenu()) {
    button->PopupClosed(aDeselectMenu);
  }
}

void XULPopupElement::ActivateItem(Element& aItemElement,
                                   const ActivateMenuItemOptions& aOptions,
                                   ErrorResult& aRv) {
  if (!Contains(&aItemElement)) {
    return aRv.ThrowInvalidStateError("Menu item is not inside this menu.");
  }

  Modifiers modifiers = ConvertModifiers(aOptions);

  // First, check if a native menu is open, and activate the item in it.
  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    if (pm->ActivateNativeMenuItem(&aItemElement, modifiers, aOptions.mButton,
                                   aRv)) {
      return;
    }
  }

  auto* item = XULButtonElement::FromNode(aItemElement);
  if (!item || !item->IsMenu()) {
    return aRv.ThrowInvalidStateError("Not a menu item");
  }

  if (!item->GetPrimaryFrame(FlushType::Frames)) {
    return aRv.ThrowInvalidStateError("Menu item is hidden");
  }

  auto* popup = item->GetContainingPopupElement();
  if (!popup) {
    return aRv.ThrowInvalidStateError("No popup");
  }

  nsMenuPopupFrame* frame = popup->GetFrame(FlushType::None);
  if (!frame || !frame->IsOpen()) {
    return aRv.ThrowInvalidStateError("Popup is not open");
  }

  // This is a chrome-only API, so we're trusted.
  const bool trusted = true;
  // KnownLive because item is aItemElement.
  MOZ_KnownLive(item)->ExecuteMenu(modifiers, aOptions.mButton, trusted);
}

void XULPopupElement::MoveTo(int32_t aLeft, int32_t aTop) {
  if (nsMenuPopupFrame* menuPopupFrame = do_QueryFrame(GetPrimaryFrame())) {
    menuPopupFrame->MoveTo(CSSIntPoint(aLeft, aTop), true);
  }
}

void XULPopupElement::MoveToAnchor(Element* aAnchorElement,
                                   const nsAString& aPosition, int32_t aXPos,
                                   int32_t aYPos, bool aAttributesOverride) {
  nsMenuPopupFrame* menuPopupFrame = GetFrame(FlushType::None);
  if (menuPopupFrame && menuPopupFrame->IsVisibleOrShowing()) {
    menuPopupFrame->MoveToAnchor(aAnchorElement, aPosition, aXPos, aYPos,
                                 aAttributesOverride);
  }
}

void XULPopupElement::SizeTo(int32_t aWidth, int32_t aHeight) {
  nsAutoCString width;
  nsAutoCString height;
  width.AppendInt(aWidth);
  width.AppendLiteral("px");
  height.AppendInt(aHeight);
  height.AppendLiteral("px");

  nsCOMPtr<nsDOMCSSDeclaration> style = Style();
  style->SetProperty("width"_ns, width, ""_ns, IgnoreErrors());
  style->SetProperty("height"_ns, height, ""_ns, IgnoreErrors());

  // If the popup is open, force a reposition of the popup after resizing it
  // with notifications set to true so that the popuppositioned event is fired.
  nsMenuPopupFrame* menuPopupFrame = do_QueryFrame(GetPrimaryFrame());
  if (menuPopupFrame && menuPopupFrame->PopupState() == ePopupShown) {
    menuPopupFrame->SetPopupPosition(false);
  }
}

void XULPopupElement::GetState(nsString& aState) {
  // set this here in case there's no frame for the popup
  aState.AssignLiteral("closed");

  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    switch (pm->GetPopupState(this)) {
      case ePopupShown:
        aState.AssignLiteral("open");
        break;
      case ePopupShowing:
      case ePopupPositioning:
      case ePopupOpening:
      case ePopupVisible:
        aState.AssignLiteral("showing");
        break;
      case ePopupHiding:
      case ePopupInvisible:
        aState.AssignLiteral("hiding");
        break;
      case ePopupClosed:
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Bad popup state");
        break;
    }
  }
}

nsINode* XULPopupElement::GetTriggerNode() const {
  nsMenuPopupFrame* menuPopupFrame = do_QueryFrame(GetPrimaryFrame());
  return nsMenuPopupFrame::GetTriggerContent(menuPopupFrame);
}

// FIXME(emilio): should probably be renamed to GetAnchorElement?
Element* XULPopupElement::GetAnchorNode() const {
  nsMenuPopupFrame* menuPopupFrame = do_QueryFrame(GetPrimaryFrame());
  if (!menuPopupFrame) {
    return nullptr;
  }
  return Element::FromNodeOrNull(menuPopupFrame->GetAnchor());
}

already_AddRefed<DOMRect> XULPopupElement::GetOuterScreenRect() {
  RefPtr<DOMRect> rect = new DOMRect(ToSupports(OwnerDoc()));

  // Return an empty rectangle if the popup is not open.
  nsMenuPopupFrame* menuPopupFrame =
      do_QueryFrame(GetPrimaryFrame(FlushType::Frames));
  if (!menuPopupFrame || !menuPopupFrame->IsOpen()) {
    return rect.forget();
  }

  Maybe<CSSRect> screenRect;

  if (menuPopupFrame->IsNativeMenu()) {
    // For native menus we can't query the true size. Use the anchor rect
    // instead, which at least has the position at which we were intending to
    // open the menu.
    screenRect = Some(CSSRect(menuPopupFrame->GetScreenAnchorRect()));
  } else if (nsIWidget* widget = menuPopupFrame->GetWidget()) {
    // For non-native menus, query the bounds from the widget.
    screenRect = Some(widget->GetScreenBounds() /
                      menuPopupFrame->PresContext()->CSSToDevPixelScale());
  }

  if (screenRect) {
    rect->SetRect(screenRect->X(), screenRect->Y(), screenRect->Width(),
                  screenRect->Height());
  }
  return rect.forget();
}

void XULPopupElement::SetConstraintRect(dom::DOMRectReadOnly& aRect) {
  nsMenuPopupFrame* menuPopupFrame =
      do_QueryFrame(GetPrimaryFrame(FlushType::Frames));
  if (menuPopupFrame) {
    menuPopupFrame->SetOverrideConstraintRect(CSSIntRect::Truncate(
        aRect.Left(), aRect.Top(), aRect.Width(), aRect.Height()));
  }
}

bool XULPopupElement::IsWaylandDragSource() const {
#ifdef MOZ_WAYLAND
  nsMenuPopupFrame* f = do_QueryFrame(GetPrimaryFrame());
  return f && f->IsDragSource();
#else
  return false;
#endif
}

bool XULPopupElement::IsWaylandPopup() const {
#ifdef MOZ_WAYLAND
  return widget::GdkIsWaylandDisplay() || widget::IsXWaylandProtocol();
#else
  return false;
#endif
}

bool XULPopupElement::IsNativeMenu() const {
  if (nsMenuPopupFrame* menuPopupFrame = do_QueryFrame(GetPrimaryFrame())) {
    return menuPopupFrame->IsNativeMenu();
  }
  return false;
}

}  // namespace mozilla::dom
