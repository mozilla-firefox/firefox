/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* global CustomTitlebar, gBrowser, gNavToolbox */

export const TabBarVisibility = {
  _initialUpdateDone: false,

  update(force = false) {
    let isPopup = !window.toolbar.visible;
    let isTaskbarTab = document.documentElement.hasAttribute("taskbartab");
    let isMiniWindow = document.documentElement.hasAttribute("mini-window");
    let isSingleTabWindow = isPopup || isTaskbarTab || isMiniWindow;

    let hasVerticalTabs =
      !isSingleTabWindow &&
      Services.prefs.getBoolPref("sidebar.verticalTabs", false);

    // When `gBrowser` has not been initialized, we're opening a new window and
    // assume only a single tab is loading.
    let hasSingleTab = !gBrowser || gBrowser.visibleTabs.length == 1;

    // To prevent tabs being lost, hiding the tabs toolbar should only work
    // when only a single tab is visible or tabs are displayed elsewhere.
    let hideTabsToolbar =
      (isSingleTabWindow && hasSingleTab) || hasVerticalTabs;

    // We only want a non-customized titlebar for popups. It should not be the
    // case, but if a popup window contains more than one tab we re-enable
    // titlebar customization and display tabs.
    CustomTitlebar.allowedBy("non-popup", !(isPopup && hasSingleTab));

    // Update the browser chrome.

    let tabsToolbar = /** @type {XULElement} */ (
      document.getElementById("TabsToolbar")
    );

    gNavToolbox.toggleAttribute("tabs-hidden", hideTabsToolbar);
    // Should the nav-bar and the toolbars underneath it look and function like
    // a titlebar? When the tabs toolbar is hidden they form the top edge of the
    // window, so they share the titlebar's background and inactive styling.
    let isTitlebar = CustomTitlebar.enabled && hideTabsToolbar;
    let titlebarIds = Services.prefs.getBoolPref("browser.nova.enabled")
      ? ["nav-bar", "PersonalToolbar"]
      : ["nav-bar"];
    for (let id of titlebarIds) {
      document
        .getElementById(id)
        .classList.toggle("browser-titlebar", isTitlebar);
    }

    if (
      hideTabsToolbar == tabsToolbar.collapsed &&
      !force &&
      this._initialUpdateDone
    ) {
      // No further updates needed, `TabsToolbar` already matches the expected
      // visibilty.
      return;
    }
    this._initialUpdateDone = true;

    tabsToolbar.collapsed = hideTabsToolbar;

    // When a window will only ever have a single tab, only show the option to
    // close the tab. FileMenu simplifies its text since we don't need to
    // disambiguate from closing the window.
    /** @type {XULElement} */ (
      document.getElementById("menu_closeWindow")
    ).hidden = isSingleTabWindow && hasSingleTab;
  },
};
