/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "NativeMenuGtk.h"
#include "AsyncDBus.h"
#include "gdk/gdkkeysyms-compat.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/XULCommandEvent.h"
#include "mozilla/WidgetUtilsGtk.h"
#include "mozilla/EventDispatcher.h"
#include "nsPresContext.h"
#include "nsIWidget.h"
#include "nsWindow.h"
#include "nsStubMutationObserver.h"
#include "mozilla/dom/Element.h"
#include "mozilla/StaticPrefs_widget.h"
#include "DBusMenu.h"
#include "nsLayoutUtils.h"
#include "nsGtkUtils.h"
#include "nsGtkKeyUtils.h"

#include <dlfcn.h>
#include <gtk/gtk.h>

#ifdef MOZ_WAYLAND
#  include "nsWaylandDisplay.h"
#endif  // MOZ_WAYLAND

namespace mozilla::widget {

using GtkMenuPopupAtRect = void (*)(GtkMenu* menu, GdkWindow* rect_window,
                                    const GdkRectangle* rect,
                                    GdkGravity rect_anchor,
                                    GdkGravity menu_anchor,
                                    const GdkEvent* trigger_event);

static bool IsDisabled(const dom::Element& aElement) {
  return aElement.GetBoolAttr(nsGkAtoms::disabled) ||
         aElement.GetBoolAttr(nsGkAtoms::hidden);
}
static bool NodeIsRelevant(const nsINode& aNode) {
  return aNode.IsAnyOfXULElements(nsGkAtoms::menu, nsGkAtoms::menuseparator,
                                  nsGkAtoms::menuitem, nsGkAtoms::menugroup,
                                  nsGkAtoms::menubar);
}

// If this is a radio / checkbox menuitem, get the current value.
static Maybe<bool> GetChecked(const dom::Element& aMenuItem) {
  static dom::Element::AttrValuesArray strings[] = {nsGkAtoms::checkbox,
                                                    nsGkAtoms::radio, nullptr};
  switch (aMenuItem.FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::type, strings,
                                    eCaseMatters)) {
    case 0:
      break;
    case 1:
      break;
    default:
      return Nothing();
  }

  return Some(aMenuItem.GetBoolAttr(nsGkAtoms::checked));
}

struct Actions {
  RefPtr<GSimpleActionGroup> mGroup;
  size_t mNextActionIndex = 0;

  nsPrintfCString Register(const dom::Element&, bool aForSubmenu);
  void Clear();
};

static MOZ_CAN_RUN_SCRIPT void ActivateItem(dom::Element& aElement) {
  if (Maybe<bool> checked = GetChecked(aElement)) {
    if (!aElement.AttrValueIs(kNameSpaceID_None, nsGkAtoms::autocheck,
                              nsGkAtoms::_false, eCaseMatters)) {
      bool newValue = !*checked;
      if (newValue) {
        aElement.SetAttr(kNameSpaceID_None, nsGkAtoms::checked, u"true"_ns,
                         true);
      } else {
        aElement.UnsetAttr(kNameSpaceID_None, nsGkAtoms::checked, true);
      }
    }
  }

  RefPtr doc = aElement.OwnerDoc();
  RefPtr event = new dom::XULCommandEvent(doc, doc->GetPresContext(), nullptr);
  IgnoredErrorResult rv;
  event->InitCommandEvent(u"command"_ns, true, true,
                          nsGlobalWindowInner::Cast(doc->GetInnerWindow()), 0,
                          /* ctrlKey = */ false, /* altKey = */ false,
                          /* shiftKey = */ false, /* cmdKey = */ false,
                          /* button = */ MouseButton::ePrimary, nullptr, 0, rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return;
  }
  aElement.DispatchEvent(*event);
}

static MOZ_CAN_RUN_SCRIPT void ActivateSignal(GSimpleAction* aAction,
                                              GVariant* aParam,
                                              gpointer aUserData) {
  RefPtr element = static_cast<dom::Element*>(aUserData);
  ActivateItem(*element);
}

static MOZ_CAN_RUN_SCRIPT void FireEvent(dom::Element* aTarget,
                                         EventMessage aPopupMessage) {
  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetMouseEvent event(true, aPopupMessage, nullptr, WidgetMouseEvent::eReal);
  EventDispatcher::Dispatch(aTarget, nullptr, &event, nullptr, &status);
}

static MOZ_CAN_RUN_SCRIPT void ChangeStateSignal(GSimpleAction* aAction,
                                                 GVariant* aParam,
                                                 gpointer aUserData) {
  // TODO: Fire events when safe. These run at a bad time for now.
  static constexpr bool kEnabled = false;
  if (!kEnabled) {
    return;
  }
  const bool open = g_variant_get_boolean(aParam);
  RefPtr popup = static_cast<dom::Element*>(aUserData);
  if (open) {
    FireEvent(popup, eXULPopupShowing);
    FireEvent(popup, eXULPopupShown);
  } else {
    FireEvent(popup, eXULPopupHiding);
    FireEvent(popup, eXULPopupHidden);
  }
}

nsPrintfCString Actions::Register(const dom::Element& aMenuItem,
                                  bool aForSubmenu) {
  nsPrintfCString actionName("item-%zu", mNextActionIndex++);
  Maybe<bool> paramValue = aForSubmenu ? Some(false) : GetChecked(aMenuItem);
  RefPtr<GSimpleAction> action;
  if (paramValue) {
    action = dont_AddRef(g_simple_action_new_stateful(
        actionName.get(), nullptr, g_variant_new_boolean(*paramValue)));
  } else {
    action = dont_AddRef(g_simple_action_new(actionName.get(), nullptr));
  }
  if (aForSubmenu) {
    g_signal_connect(action, "change-state", G_CALLBACK(ChangeStateSignal),
                     gpointer(&aMenuItem));
  } else {
    g_signal_connect(action, "activate", G_CALLBACK(ActivateSignal),
                     gpointer(&aMenuItem));
  }
  g_action_map_add_action(G_ACTION_MAP(mGroup.get()), G_ACTION(action.get()));
  return actionName;
}

void Actions::Clear() {
  for (size_t i = 0; i < mNextActionIndex; ++i) {
    g_action_map_remove_action(G_ACTION_MAP(mGroup.get()),
                               nsPrintfCString("item-%zu", i).get());
  }
  mNextActionIndex = 0;
}

class MenuModel : public nsStubMutationObserver {
  NS_DECL_ISUPPORTS

  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED

 public:
  explicit MenuModel(dom::Element* aElement) : mElement(aElement) {
    mElement->AddMutationObserver(this);
  }

  dom::Element* Element() { return mElement; }

  void RecomputeModelIfNeeded() {
    if (!mDirty) {
      return;
    }
    RecomputeModel();
    mDirty = false;
  }

  bool IsShowing() { return mShowing; }
  void WillShow() {
    mShowing = true;
    RecomputeModelIfNeeded();
  }
  void DidHide() { mShowing = false; }

 protected:
  virtual void RecomputeModel() = 0;
  virtual ~MenuModel() { mElement->RemoveMutationObserver(this); }

  void DirtyModel() {
    mDirty = true;
    if (mShowing) {
      RecomputeModelIfNeeded();
    }
  }

  RefPtr<dom::Element> mElement;
  bool mDirty = true;
  bool mShowing = false;
};

class MenuModelGMenu final : public MenuModel {
 public:
  explicit MenuModelGMenu(dom::Element* aElement) : MenuModel(aElement) {
    mGMenu = dont_AddRef(g_menu_new());
    mActions.mGroup = dont_AddRef(g_simple_action_group_new());
  }

  GMenuModel* GetModel() { return G_MENU_MODEL(mGMenu.get()); }
  GActionGroup* GetActionGroup() {
    return G_ACTION_GROUP(mActions.mGroup.get());
  }

 protected:
  void RecomputeModel() override;
  static void RecomputeModelFor(GMenu* aMenu, Actions& aActions,
                                const dom::Element& aElement);

  RefPtr<GMenu> mGMenu;
  Actions mActions;
};

NS_IMPL_ISUPPORTS(MenuModel, nsIMutationObserver)

void MenuModel::ContentWillBeRemoved(nsIContent* aChild,
                                     const ContentRemoveInfo&) {
  if (NodeIsRelevant(*aChild)) {
    nsContentUtils::AddScriptRunner(NewRunnableMethod(
        "MenuModel::ContentWillBeRemoved", this, &MenuModel::DirtyModel));
  }
}

void MenuModel::ContentInserted(nsIContent* aChild, const ContentInsertInfo&) {
  if (NodeIsRelevant(*aChild)) {
    DirtyModel();
  }
}

void MenuModel::ContentAppended(nsIContent* aChild, const ContentAppendInfo&) {
  if (NodeIsRelevant(*aChild)) {
    DirtyModel();
  }
}

void MenuModel::AttributeChanged(dom::Element* aElement, int32_t,
                                 nsAtom* aAttribute, AttrModType,
                                 const nsAttrValue*) {
  if (NodeIsRelevant(*aElement) &&
      (aAttribute == nsGkAtoms::label || aAttribute == nsGkAtoms::aria_label ||
       aAttribute == nsGkAtoms::disabled || aAttribute == nsGkAtoms::hidden)) {
    DirtyModel();
  }
}

static const dom::Element* GetMenuPopupChild(const dom::Element& aElement) {
  for (const nsIContent* child = aElement.GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsXULElement(nsGkAtoms::menupopup)) {
      return child->AsElement();
    }
  }
  return nullptr;
}

void MenuModelGMenu::RecomputeModelFor(GMenu* aMenu, Actions& aActions,
                                       const dom::Element& aElement) {
  RefPtr<GMenu> sectionMenu;
  auto FlushSectionMenu = [&] {
    if (sectionMenu) {
      g_menu_append_section(aMenu, nullptr, G_MENU_MODEL(sectionMenu.get()));
      sectionMenu = nullptr;
    }
  };

  for (const nsIContent* child = aElement.GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsXULElement(nsGkAtoms::menuitem) &&
        !IsDisabled(*child->AsElement())) {
      nsAutoString label;
      child->AsElement()->GetAttr(nsGkAtoms::label, label);
      if (label.IsEmpty()) {
        child->AsElement()->GetAttr(nsGkAtoms::aria_label, label);
      }
      nsPrintfCString actionName(
          "menu.%s",
          aActions.Register(*child->AsElement(), /* aForSubmenu = */ false)
              .get());
      g_menu_append(sectionMenu ? sectionMenu.get() : aMenu,
                    NS_ConvertUTF16toUTF8(label).get(), actionName.get());
      continue;
    }
    if (child->IsXULElement(nsGkAtoms::menuseparator)) {
      FlushSectionMenu();
      sectionMenu = dont_AddRef(g_menu_new());
      continue;
    }
    if (child->IsXULElement(nsGkAtoms::menugroup)) {
      FlushSectionMenu();
      sectionMenu = dont_AddRef(g_menu_new());
      RecomputeModelFor(sectionMenu, aActions, *child->AsElement());
      FlushSectionMenu();
      continue;
    }
    if (child->IsXULElement(nsGkAtoms::menu) &&
        !IsDisabled(*child->AsElement())) {
      if (const auto* popup = GetMenuPopupChild(*child->AsElement())) {
        RefPtr<GMenu> submenu = dont_AddRef(g_menu_new());
        RecomputeModelFor(submenu, aActions, *popup);
        nsAutoString label;
        child->AsElement()->GetAttr(nsGkAtoms::label, label);
        RefPtr<GMenuItem> submenuItem = dont_AddRef(g_menu_item_new_submenu(
            NS_ConvertUTF16toUTF8(label).get(), G_MENU_MODEL(submenu.get())));
        nsPrintfCString actionName(
            "menu.%s",
            aActions.Register(*popup, /* aForSubmenu = */ true).get());
        g_menu_item_set_attribute_value(submenuItem.get(), "submenu-action",
                                        g_variant_new_string(actionName.get()));
        g_menu_append_item(sectionMenu ? sectionMenu.get() : aMenu,
                           submenuItem.get());
      }
    }
  }

  FlushSectionMenu();
}

void MenuModelGMenu::RecomputeModel() {
  mActions.Clear();
  g_menu_remove_all(mGMenu.get());
  RecomputeModelFor(mGMenu.get(), mActions, *mElement);
}

static GtkMenuPopupAtRect GetPopupAtRectFn() {
  static GtkMenuPopupAtRect sFunc =
      (GtkMenuPopupAtRect)dlsym(RTLD_DEFAULT, "gtk_menu_popup_at_rect");
  return sFunc;
}

bool NativeMenuGtk::CanUse() {
  return StaticPrefs::widget_gtk_native_context_menus() && GetPopupAtRectFn();
}

void NativeMenuGtk::FireEvent(EventMessage aPopupMessage) {
  RefPtr target = Element();
  widget::FireEvent(target, aPopupMessage);
}

#define METHOD_SIGNAL(name_)                                 \
  static MOZ_CAN_RUN_SCRIPT_BOUNDARY void On##name_##Signal( \
      GtkWidget* widget, gpointer user_data) {               \
    RefPtr menu = static_cast<NativeMenuGtk*>(user_data);    \
    return menu->On##name_();                                \
  }

METHOD_SIGNAL(Unmap);

#undef METHOD_SIGNAL

NativeMenuGtk::NativeMenuGtk(dom::Element* aElement)
    : mMenuModel(MakeRefPtr<MenuModelGMenu>(aElement)) {
  // Floating, so no need to dont_AddRef.
  mNativeMenu = gtk_menu_new_from_model(mMenuModel->GetModel());
  gtk_widget_insert_action_group(mNativeMenu.get(), "menu",
                                 mMenuModel->GetActionGroup());
  g_signal_connect(mNativeMenu, "unmap", G_CALLBACK(OnUnmapSignal), this);
}

NativeMenuGtk::~NativeMenuGtk() {
  g_signal_handlers_disconnect_by_data(mNativeMenu, this);
}

RefPtr<dom::Element> NativeMenuGtk::Element() { return mMenuModel->Element(); }

void NativeMenuGtk::ShowMenuAnchored(nsIFrame* aClickedFrame,
                                     const nsMenuPopupFrame* aPopupFrame) {
  MOZ_ASSERT_UNREACHABLE("GTK native anchored menus are not implemented");
}

void NativeMenuGtk::ShowMenuAtPosition(nsIFrame* aClickedFrame,
                                       const CSSIntPoint& aPosition,
                                       bool aIsContextMenu) {
  if (mMenuModel->IsShowing()) {
    return;
  }
  RefPtr<nsIWidget> widget = aClickedFrame->PresContext()->GetRootWidget();
  if (NS_WARN_IF(!widget)) {
    // XXX Do we need to close menus here?
    return;
  }
  auto* win = static_cast<GdkWindow*>(widget->GetNativeData(NS_NATIVE_WINDOW));
  if (NS_WARN_IF(!win)) {
    return;
  }

  auto* geckoWin = nsWindow::FromWidget(widget);

  // The position needs to be relative to our window.
  auto pos = (aPosition * aClickedFrame->PresContext()->CSSToDevPixelScale()) -
             geckoWin->WidgetToScreenOffset();
  auto gdkPos = geckoWin->DevicePixelsToGdkPointRoundDown(
      LayoutDeviceIntPoint::Round(pos));

  mMenuModel->WillShow();
  const GdkRectangle rect = {gdkPos.x, gdkPos.y, 1, 1};
  auto openFn = GetPopupAtRectFn();
  openFn(GTK_MENU(mNativeMenu.get()), win, &rect, GDK_GRAVITY_NORTH_WEST,
         GDK_GRAVITY_NORTH_WEST, GetLastPointerDownEvent());

  RefPtr pin{this};
  FireEvent(eXULPopupShown);
}

bool NativeMenuGtk::Close() {
  if (!mMenuModel->IsShowing()) {
    return false;
  }
  gtk_menu_popdown(GTK_MENU(mNativeMenu.get()));
  return true;
}

void NativeMenuGtk::OnUnmap() {
  FireEvent(eXULPopupHiding);

  mMenuModel->DidHide();

  FireEvent(eXULPopupHidden);

  for (NativeMenu::Observer* observer : mObservers.Clone()) {
    observer->OnNativeMenuClosed();
  }
}

void NativeMenuGtk::ActivateItem(dom::Element* aItemElement, Modifiers,
                                 int16_t aButton, ErrorResult&) {
  // TODO: For testing only.
}

void NativeMenuGtk::OpenSubmenu(dom::Element*) {
  // TODO: For testing mostly.
}

void NativeMenuGtk::CloseSubmenu(dom::Element*) {
  // TODO: For testing mostly.
}

#ifdef MOZ_ENABLE_DBUS

class MenubarModelDBus final : public MenuModel {
 public:
  explicit MenubarModelDBus(dom::Element* aElement) : MenuModel(aElement) {
    mRoot = dont_AddRef(dbusmenu_menuitem_new());
    dbusmenu_menuitem_set_root(mRoot.get(), true);
    mShowing = true;
  }

  DbusmenuMenuitem* Root() const { return mRoot.get(); }

 protected:
  void RecomputeModel() override;
  static void AppendMenuItem(DbusmenuMenuitem* aParent,
                             const dom::Element* aElement);
  static void AppendSeparator(DbusmenuMenuitem* aParent);
  static void AppendSubmenu(DbusmenuMenuitem* aParent,
                            const dom::Element* aMenu,
                            const dom::Element* aPopup);
  static uint RecomputeModelFor(DbusmenuMenuitem* aParent,
                                const dom::Element& aElement);

  RefPtr<DbusmenuMenuitem> mRoot;
};

void MenubarModelDBus::RecomputeModel() {
  while (GList* children = dbusmenu_menuitem_get_children(mRoot.get())) {
    auto* first = static_cast<DbusmenuMenuitem*>(children->data);
    if (!first) {
      break;
    }
    dbusmenu_menuitem_child_delete(mRoot.get(), first);
  }
  RecomputeModelFor(mRoot, *Element());
}

static const dom::Element* RelevantElementForKeys(
    const dom::Element* aElement) {
  nsAutoString key;
  aElement->GetAttr(nsGkAtoms::key, key);
  if (!key.IsEmpty()) {
    dom::Document* document = aElement->OwnerDoc();
    dom::Element* element = document->GetElementById(key);
    if (element) {
      return element;
    }
  }
  return aElement;
}

static uint32_t ParseKey(const nsAString& aKey, const nsAString& aKeyCode) {
  guint key = 0;
  if (!aKey.IsEmpty()) {
    key = gdk_unicode_to_keyval(*aKey.BeginReading());
  }

  if (key == 0 && !aKeyCode.IsEmpty()) {
    key = KeymapWrapper::ConvertGeckoKeyCodeToGDKKeyval(aKeyCode);
  }

  return key;
}

static uint32_t KeyFrom(const dom::Element* aElement) {
  const auto* element = RelevantElementForKeys(aElement);

  nsAutoString key;
  nsAutoString keycode;
  element->GetAttr(nsGkAtoms::key, key);
  element->GetAttr(nsGkAtoms::keycode, keycode);

  return ParseKey(key, keycode);
}

// TODO(emilio): Unify with nsMenuUtilsX::GeckoModifiersForNodeAttribute (or
// at least switch to strtok_r).
static uint32_t ParseModifiers(const nsAString& aModifiers) {
  if (aModifiers.IsEmpty()) {
    return 0;
  }

  uint32_t modifier = 0;
  char* str = ToNewUTF8String(aModifiers);
  char* token = strtok(str, ", \t");
  while (token) {
    if (nsCRT::strcmp(token, "shift") == 0) {
      modifier |= GDK_SHIFT_MASK;
    } else if (nsCRT::strcmp(token, "alt") == 0) {
      modifier |= GDK_MOD1_MASK;
    } else if (nsCRT::strcmp(token, "meta") == 0) {
      modifier |= GDK_META_MASK;
    } else if (nsCRT::strcmp(token, "control") == 0) {
      modifier |= GDK_CONTROL_MASK;
    } else if (nsCRT::strcmp(token, "accel") == 0) {
      auto accel = WidgetInputEvent::AccelModifier();
      if (accel == MODIFIER_META) {
        modifier |= GDK_META_MASK;
      } else if (accel == MODIFIER_ALT) {
        modifier |= GDK_MOD1_MASK;
      } else if (accel == MODIFIER_CONTROL) {
        modifier |= GDK_CONTROL_MASK;
      }
    }

    token = strtok(nullptr, ", \t");
  }

  free(str);

  return modifier;
}

static uint32_t ModifiersFrom(const dom::Element* aContent) {
  const auto* element = RelevantElementForKeys(aContent);

  nsAutoString modifiers;
  element->GetAttr(nsGkAtoms::modifiers, modifiers);

  return ParseModifiers(modifiers);
}

static void UpdateAccel(DbusmenuMenuitem* aItem, const nsIContent* aContent) {
  uint32_t key = KeyFrom(aContent->AsElement());
  if (key != 0) {
    dbusmenu_menuitem_property_set_shortcut(
        aItem, key,
        static_cast<GdkModifierType>(ModifiersFrom(aContent->AsElement())));
  }
}

static void UpdateRadioOrCheck(DbusmenuMenuitem* aItem,
                               const dom::Element* aContent) {
  static mozilla::dom::Element::AttrValuesArray attrs[] = {
      nsGkAtoms::checkbox, nsGkAtoms::radio, nullptr};
  int32_t type = aContent->FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::type,
                                           attrs, eCaseMatters);

  if (type < 0 || type >= 2) {
    return;
  }

  if (type == 0) {
    dbusmenu_menuitem_property_set(aItem, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE,
                                   DBUSMENU_MENUITEM_TOGGLE_CHECK);
  } else {
    dbusmenu_menuitem_property_set(aItem, DBUSMENU_MENUITEM_PROP_TOGGLE_TYPE,
                                   DBUSMENU_MENUITEM_TOGGLE_RADIO);
  }

  const bool isChecked = aContent->GetBoolAttr(nsGkAtoms::checked);
  dbusmenu_menuitem_property_set_int(
      aItem, DBUSMENU_MENUITEM_PROP_TOGGLE_STATE,
      isChecked ? DBUSMENU_MENUITEM_TOGGLE_STATE_CHECKED
                : DBUSMENU_MENUITEM_TOGGLE_STATE_UNCHECKED);
}

static void UpdateEnabled(DbusmenuMenuitem* aItem, const nsIContent* aContent) {
  const bool disabled = aContent->AsElement()->GetBoolAttr(nsGkAtoms::disabled);
  dbusmenu_menuitem_property_set_bool(aItem, DBUSMENU_MENUITEM_PROP_ENABLED,
                                      !disabled);
}

// we rebuild the dbus model when elements are removed from the DOM,
// so this isn't going to trigger for asynchronous
static MOZ_CAN_RUN_SCRIPT void DBusActivationCallback(
    DbusmenuMenuitem* aMenuitem, guint aTimestamp, gpointer aUserData) {
  RefPtr element = static_cast<dom::Element*>(aUserData);
  ActivateItem(*element);
}

static void ConnectActivated(DbusmenuMenuitem* aItem,
                             const dom::Element* aContent) {
  g_signal_connect(G_OBJECT(aItem), DBUSMENU_MENUITEM_SIGNAL_ITEM_ACTIVATED,
                   G_CALLBACK(DBusActivationCallback),
                   const_cast<dom::Element*>(aContent));
}

static MOZ_CAN_RUN_SCRIPT bool DBusEventCallback(DbusmenuMenuitem* aMenuItem,
                                                 const gchar* name,
                                                 GVariant* variant,
                                                 guint timestamp,
                                                 gpointer aUserData) {
  RefPtr element = static_cast<dom::Element*>(aUserData);
  if (strcmp(name, "opened") == 0) {
    FireEvent(element, eXULPopupShowing);
    FireEvent(element, eXULPopupShown);
  } else if (strcmp(name, "closed") == 0) {
    FireEvent(element, eXULPopupHiding);
    FireEvent(element, eXULPopupHidden);
  }
  return false;
}

static void ConnectEvent(DbusmenuMenuitem* aItem,
                         const dom::Element* aContent) {
  g_signal_connect(G_OBJECT(aItem), DBUSMENU_MENUITEM_SIGNAL_EVENT,
                   G_CALLBACK(DBusEventCallback),
                   const_cast<dom::Element*>(aContent));
}

void MenubarModelDBus::AppendMenuItem(DbusmenuMenuitem* aParent,
                                      const dom::Element* aChild) {
  nsAutoString label;
  aChild->GetAttr(nsGkAtoms::label, label);
  if (label.IsEmpty()) {
    aChild->GetAttr(nsGkAtoms::aria_label, label);
  }
  RefPtr<DbusmenuMenuitem> child = dont_AddRef(dbusmenu_menuitem_new());
  dbusmenu_menuitem_property_set(child, DBUSMENU_MENUITEM_PROP_LABEL,
                                 NS_ConvertUTF16toUTF8(label).get());
  dbusmenu_menuitem_child_append(aParent, child);
  UpdateAccel(child, aChild);
  UpdateRadioOrCheck(child, aChild);
  UpdateEnabled(child, aChild);
  ConnectActivated(child, aChild);
  // TODO: icons
}

void MenubarModelDBus::AppendSeparator(DbusmenuMenuitem* aParent) {
  RefPtr<DbusmenuMenuitem> child = dont_AddRef(dbusmenu_menuitem_new());
  dbusmenu_menuitem_property_set(child, DBUSMENU_MENUITEM_PROP_TYPE,
                                 "separator");
  dbusmenu_menuitem_child_append(aParent, child);
}

void MenubarModelDBus::AppendSubmenu(DbusmenuMenuitem* aParent,
                                     const dom::Element* aMenu,
                                     const dom::Element* aPopup) {
  RefPtr<DbusmenuMenuitem> submenu = dont_AddRef(dbusmenu_menuitem_new());
  if (RecomputeModelFor(submenu, *aPopup) == 0) {
    RefPtr<DbusmenuMenuitem> placeholder = dont_AddRef(dbusmenu_menuitem_new());
    dbusmenu_menuitem_child_append(submenu, placeholder);
  }
  nsAutoString label;
  aMenu->GetAttr(nsGkAtoms::label, label);
  ConnectEvent(submenu, aPopup);
  dbusmenu_menuitem_property_set(submenu, DBUSMENU_MENUITEM_PROP_LABEL,
                                 NS_ConvertUTF16toUTF8(label).get());
  dbusmenu_menuitem_child_append(aParent, submenu);
}

uint MenubarModelDBus::RecomputeModelFor(DbusmenuMenuitem* aParent,
                                         const dom::Element& aElement) {
  uint childCount = 0;
  for (const nsIContent* child = aElement.GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsXULElement(nsGkAtoms::menuitem) &&
        !IsDisabled(*child->AsElement())) {
      AppendMenuItem(aParent, child->AsElement());
      childCount++;
      continue;
    }
    if (child->IsXULElement(nsGkAtoms::menuseparator)) {
      AppendSeparator(aParent);
      childCount++;
      continue;
    }
    if (child->IsXULElement(nsGkAtoms::menu) &&
        !IsDisabled(*child->AsElement())) {
      if (const auto* popup = GetMenuPopupChild(*child->AsElement())) {
        childCount++;
        AppendSubmenu(aParent, child->AsElement(), popup);
      }
    }
  }
  return childCount;
}

void DBusMenuBar::NameOwnerChangedCallback(GObject*, GParamSpec*,
                                           gpointer user_data) {
  static_cast<DBusMenuBar*>(user_data)->OnNameOwnerChanged();
}

void DBusMenuBar::OnNameOwnerChanged() {
  GUniquePtr<gchar> nameOwner(g_dbus_proxy_get_name_owner(mProxy));
  if (!nameOwner) {
    return;
  }

  RefPtr win = mMenuModel->Element()->OwnerDoc()->GetInnerWindow();
  if (NS_WARN_IF(!win)) {
    return;
  }
  nsIWidget* widget = nsGlobalWindowInner::Cast(win.get())->GetNearestWidget();
  if (NS_WARN_IF(!widget)) {
    return;
  }
  auto* gdkWin =
      static_cast<GdkWindow*>(widget->GetNativeData(NS_NATIVE_WINDOW));
  if (NS_WARN_IF(!gdkWin)) {
    return;
  }

  gdkWin = gdk_window_get_toplevel(gdkWin);

#  ifdef MOZ_WAYLAND
  if (auto* display = widget::WaylandDisplayGet()) {
    if (!StaticPrefs::widget_gtk_global_menu_wayland_enabled()) {
      return;
    }
    org_kde_kwin_appmenu_manager* appMenuManager = display->GetAppMenuManager();
    if (NS_WARN_IF(!appMenuManager)) {
      return;
    }

    wl_surface* surface = gdk_wayland_window_get_wl_surface(gdkWin);
    if (NS_WARN_IF(!surface)) {
      return;
    }

    GDBusConnection* connection = g_dbus_proxy_get_connection(mProxy);
    const char* myServiceName = g_dbus_connection_get_unique_name(connection);
    if (NS_WARN_IF(!myServiceName)) {
      return;
    }

    if (!mAppMenu) {
      mAppMenu = org_kde_kwin_appmenu_manager_create(appMenuManager, surface);
    }

    mMenuModel->Element()->SetBoolAttr(nsGkAtoms::native, true);
    org_kde_kwin_appmenu_set_address(mAppMenu, myServiceName,
                                     mObjectPath.get());
    return;
  }
#  endif
#  ifdef MOZ_X11
  // legacy path
  auto xid = GDK_WINDOW_XID(gdkWin);
  widget::DBusProxyCall(mProxy, "RegisterWindow",
                        g_variant_new("(uo)", xid, mObjectPath.get()),
                        G_DBUS_CALL_FLAGS_NONE)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}](RefPtr<GVariant>&& aResult) {
            self->mMenuModel->Element()->SetBoolAttr(nsGkAtoms::native, true);
          },
          [self = RefPtr{this}](GUniquePtr<GError>&& aError) {
            g_printerr("Failed to register window menubar: %s\n",
                       aError->message);
            self->mMenuModel->Element()->SetBoolAttr(nsGkAtoms::native, false);
          });
#  endif
}

static unsigned sID = 0;

DBusMenuBar::DBusMenuBar(dom::Element* aElement)
    : mObjectPath(nsPrintfCString("/com/canonical/menu/%u", sID++)),
      mMenuModel(MakeRefPtr<MenubarModelDBus>(aElement)),
      mServer(dont_AddRef(dbusmenu_server_new(mObjectPath.get()))) {
  mMenuModel->RecomputeModelIfNeeded();
  dbusmenu_server_set_root(mServer.get(), mMenuModel->Root());
}

RefPtr<DBusMenuBar> DBusMenuBar::Create(dom::Element* aElement) {
  RefPtr<DBusMenuBar> self = new DBusMenuBar(aElement);
  widget::CreateDBusProxyForBus(
      G_BUS_TYPE_SESSION,
      GDBusProxyFlags(G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES |
                      G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS |
                      G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START),
      nullptr, "com.canonical.AppMenu.Registrar",
      "/com/canonical/AppMenu/Registrar", "com.canonical.AppMenu.Registrar")
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self](RefPtr<GDBusProxy>&& aProxy) {
            self->mProxy = std::move(aProxy);
            g_signal_connect(self->mProxy, "notify::g-name-owner",
                             G_CALLBACK(NameOwnerChangedCallback), self.get());
            self->OnNameOwnerChanged();
          },
          [](GUniquePtr<GError>&& aError) {
            g_printerr("Failed to create DBUS proxy for menubar: %s\n",
                       aError->message);
          });
  return self;
}

DBusMenuBar::~DBusMenuBar() {
#  ifdef MOZ_WAYLAND
  MozClearPointer(mAppMenu, org_kde_kwin_appmenu_release);
#  endif
}
#endif

}  // namespace mozilla::widget
