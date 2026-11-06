/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  CustomizableUI:
    "moz-src:///browser/components/customizableui/CustomizableUI.sys.mjs",
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

const WIDGET_ID = "tab-groups-button";
const ANCHOR_WIDGET_ID = "alltabs-button";
const EXTENSIONS_WIDGET_ID = "unified-extensions-button";
const WIDGET_ENABLED_PREF = "browser.toolbars.tab-groups.enabled";
const INTRODUCED_PREF = "browser.toolbarbuttons.introduced.tab-groups-button";
const REMOVED_HINT_SHOWN_PREF = "browser.tabs.groups.removedHintShown";
const ALTERNATE_MENU_PREF = "browser.tabs.groups.alternateMenu";

/**
 * Adds the Tab Groups button to the toolbar once per profile, unless the
 * user has already placed or removed it, and points the user to the
 * application menu the first time they remove it.
 */
export const TabGroupsToolbarButton = {
  /**
   * Called at idle startup. Listens for the button being removed, and adds
   * the button if there are saved tab groups.
   */
  init() {
    if (!Services.prefs.getBoolPref(WIDGET_ENABLED_PREF, false)) {
      return;
    }

    let buttonIntroduced = Services.prefs.getBoolPref(INTRODUCED_PREF, false);
    let buttonRemovedHintShown = Services.prefs.getBoolPref(
      REMOVED_HINT_SHOWN_PREF,
      false
    );
    if (buttonIntroduced && buttonRemovedHintShown) {
      return;
    }

    lazy.CustomizableUI.addListener(this);
    if (!buttonIntroduced && lazy.SessionStore.getSavedTabGroups().length) {
      this.maybeAddTabGroupsButton();
    }
  },
  /**
   * Places the button after the List All Tabs button, or before the
   * extensions button if List All Tabs isn't placed somewhere visible.
   */
  maybeAddTabGroupsButton() {
    if (
      !Services.prefs.getBoolPref(WIDGET_ENABLED_PREF, false) ||
      Services.prefs.getBoolPref(INTRODUCED_PREF, false)
    ) {
      return;
    }
    if (!lazy.CustomizableUI.getPlacementOfWidget(WIDGET_ID)) {
      let anchor = lazy.CustomizableUI.getPlacementOfWidget(ANCHOR_WIDGET_ID);
      let win = lazy.BrowserWindowTracker.getTopWindow({
        allowFromInactiveWorkspace: true,
      });
      if (
        anchor &&
        win &&
        lazy.CustomizableUI.widgetIsLikelyVisible(ANCHOR_WIDGET_ID, win)
      ) {
        lazy.CustomizableUI.addWidgetToArea(
          WIDGET_ID,
          anchor.area,
          anchor.position + 1
        );
      } else {
        let extensions =
          lazy.CustomizableUI.getPlacementOfWidget(EXTENSIONS_WIDGET_ID);
        lazy.CustomizableUI.addWidgetToArea(
          WIDGET_ID,
          lazy.CustomizableUI.AREA_NAVBAR,
          extensions?.position
        );
      }
    }
    Services.prefs.setBoolPref(INTRODUCED_PREF, true);
  },
  /**
   * Points the user to the application menu the first time they remove the
   * button.
   */
  maybeShowRemovedHint() {
    if (
      Services.prefs.getBoolPref(REMOVED_HINT_SHOWN_PREF, false) ||
      !Services.prefs.getBoolPref(ALTERNATE_MENU_PREF, false) ||
      lazy.CustomizableUI.getPlacementOfWidget(WIDGET_ID)
    ) {
      return;
    }
    let win = lazy.BrowserWindowTracker.getTopWindow({
      allowFromInactiveWorkspace: true,
    });
    let anchor = win?.document.getElementById("PanelUI-menu-button");
    if (!anchor?.checkVisibility({ visibilityProperty: true })) {
      return;
    }
    win.ConfirmationHint.show(
      anchor,
      "confirmation-hint-tab-group-button-removed",
      {
        hideCheckmark: true,
        position: "bottomright topright",
      }
    );
    Services.prefs.setBoolPref(REMOVED_HINT_SHOWN_PREF, true);
  },
  /**
   * CustomizableUI listener, called when any widget is removed from an area.
   */
  async onWidgetRemoved(widgetId) {
    if (widgetId != WIDGET_ID) {
      return;
    }

    Services.prefs.setBoolPref(INTRODUCED_PREF, true);
    await Promise.resolve();
    this.maybeShowRemovedHint();
  },
};
