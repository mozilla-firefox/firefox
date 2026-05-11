/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_widget_NativeMenuGtk_h
#define mozilla_widget_NativeMenuGtk_h

#include "mozilla/RefCounted.h"
#include "mozilla/widget/NativeMenu.h"
#include "mozilla/EventForwards.h"
#include "GRefPtr.h"

struct org_kde_kwin_appmenu;

namespace mozilla {

namespace dom {
class Element;
}

namespace widget {

class MenuModelGMenu;
class MenubarModelDBus;

class NativeMenuGtk : public NativeMenu {
 public:
  // Whether we can use native menu popups on GTK.
  static bool CanUse();

  explicit NativeMenuGtk(dom::Element* aElement);

  // NativeMenu
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void ShowMenuAnchored(
      nsIFrame* aClickedFrame, const nsMenuPopupFrame* aPopupFrame) override;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void ShowMenuAtPosition(
      nsIFrame* aClickedFrame, const CSSIntPoint& aPosition,
      bool aIsContextMenu) override;
  bool Close() override;
  void ActivateItem(dom::Element* aItemElement, Modifiers aModifiers,
                    int16_t aButton, ErrorResult& aRv) override;
  void OpenSubmenu(dom::Element* aMenuElement) override;
  void CloseSubmenu(dom::Element* aMenuElement) override;
  RefPtr<dom::Element> Element() override;
  void AddObserver(NativeMenu::Observer* aObserver) override {
    mObservers.AppendElement(aObserver);
  }
  void RemoveObserver(NativeMenu::Observer* aObserver) override {
    mObservers.RemoveElement(aObserver);
  }

  MOZ_CAN_RUN_SCRIPT void OnUnmap();

 protected:
  virtual ~NativeMenuGtk();

  MOZ_CAN_RUN_SCRIPT void FireEvent(EventMessage);

  bool mPoppedUp = false;
  RefPtr<GtkWidget> mNativeMenu;
  RefPtr<MenuModelGMenu> mMenuModel;
  nsTArray<NativeMenu::Observer*> mObservers;
};

#ifdef MOZ_ENABLE_DBUS

class DBusMenuBar final : public RefCounted<DBusMenuBar> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(DBusMenuBar)
  static RefPtr<DBusMenuBar> Create(dom::Element*);
  ~DBusMenuBar();

 protected:
  explicit DBusMenuBar(dom::Element* aElement);

  static void NameOwnerChangedCallback(GObject*, GParamSpec*, gpointer);
  void OnNameOwnerChanged();

  nsCString mObjectPath;
  RefPtr<MenubarModelDBus> mMenuModel;
  RefPtr<DbusmenuServer> mServer;
  RefPtr<GDBusProxy> mProxy;
#  ifdef MOZ_WAYLAND
  org_kde_kwin_appmenu* mAppMenu = nullptr;
#  endif
};

#endif

}  // namespace widget
}  // namespace mozilla

#endif
