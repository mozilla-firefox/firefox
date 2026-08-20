/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { setTimeout } from "resource://gre/modules/Timer.sys.mjs";
import { TestUtils } from "resource://testing-common/TestUtils.sys.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  BrowserTestUtils: "resource://testing-common/BrowserTestUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  SidebarTestUtils: "resource://testing-common/SidebarTestUtils.sys.mjs",
});

const PRINCIPAL = Services.scriptSecurityManager.getSystemPrincipal();
const HOME_PAGE = "resource://mozscreenshots/lib/mozscreenshots.html";

// Reuses the shared tab-audio-indicator fixture instead of adding a new one.
const AUDIO_URL =
  "https://example.com/browser/browser/components/tabbrowser/test/browser/tabs/audio.ogg";
const AUDIO_PAGE = `data:text/html,<audio src="${AUDIO_URL}" autoplay loop></audio>`;

export var Nova = {
  init() {},

  configurations: {
    // IDs match the shot list attached to bug 2050672. Gaps are scenarios
    // that this configuration set doesn't implement.
    // Core
    // 01 - Classic Window, example.com, one tab
    chrome: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
      },
    },

    // 02 - Sidebar, Bookmarks panel open
    sidebar: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await win.SidebarController.show("viewBookmarksSidebar");
      },
    },

    // 03 - Vertical tabs
    verticalTabs: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await enableVerticalTabs(win);
      },
    },

    // 04 - Tab groups: 3 tabs, group "QA", expanded
    tabGroupExpanded: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        let group = addTabGroup(win, 3, false);
        group.collapsed = false;
      },
    },

    // 05 - Nova New Tab
    newTab: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, "about:newtab");
      },
    },

    // 06 - Private Window
    privateWindow: {
      selectors: [":root"],
      async applyConfig() {
        await reset();
        // about:privatebrowsing is part of what this shot is about, so leave
        // it in place rather than navigating to HOME_PAGE.
        await lazy.BrowserTestUtils.openNewBrowserWindow({
          private: true,
          waitForTabURL: "about:privatebrowsing",
        });
      },
    },

    // 08 - Urlbar: focused with query "firefox nova"
    urlbarSearch: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        win.gURLBar.search("firefox nova");
      },
    },

    // 09 - Onboarding: About Welcome
    firstRun: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, "about:welcome");
      },
    },

    // 10 - Onboarding: post-first-run profile (About Welcome already seen)
    continuous: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        Services.prefs.setBoolPref("browser.aboutwelcome.enabled", false);
        await navigateTo(win, "about:newtab");
      },
    },

    // 11 - Settings: about:preferences#sync
    settings: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, "about:preferences#sync");
      },
    },

    // 12 - Customize toolbar
    customizeToolbar: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await enterCustomizeMode(win);
      },
    },

    // Shot 13 (chrome, dark theme) isn't a distinct scenario: it's already
    // produced by crossing `chrome` with the ColorScheme `dark` configuration.

    // 14 - Sidebar: History panel
    sidebarHistory: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await win.SidebarController.show("viewHistorySidebar");
      },
    },

    // 15 - Vertical tabs overflow: 15+ tabs
    verticalTabsOverflow: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await enableVerticalTabs(win);
        await loadTabsUntilOverflow(win, 15);
      },
    },

    // 16 - Tab groups: group "QA" collapsed
    tabGroupCollapsed: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        let group = addTabGroup(win, 3, false);
        group.collapsed = true;
      },
    },

    // 17 - Media tab: audio playing in a background tab, example.com in foreground
    chromeMediaBackground: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await openAudioTab(win, { background: true });
      },
    },

    // Extended
    // B01 - Application menu open
    appMenu: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        let promiseViewShown = lazy.BrowserTestUtils.waitForEvent(
          win.PanelUI.panel,
          "ViewShown"
        );
        win.PanelUI.show();
        await promiseViewShown;
      },
    },

    // B02 - Classic Window / Smart Window switcher open
    switcherMenu: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        Services.prefs.setBoolPref("browser.smartwindow.enabled", true);
        await TestUtils.waitForCondition(
          () => !win.document.getElementById("ai-window-toggle")?.hidden,
          "waiting for the window switcher button"
        );
        let button = win.document.getElementById("ai-window-toggle");
        let view = win.PanelMultiView.getViewNode(
          win.document,
          "ai-window-toggle-view"
        );
        let viewShown = lazy.BrowserTestUtils.waitForEvent(view, "ViewShown");
        button.click();
        await viewShown;
      },
    },

    // B04 - Tab chrome: mute control hovered on an audio-playing tab
    tabMuteHover: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        let tab = await openAudioTab(win, { background: false });
        hover(tab.audioButton);
      },
    },

    // B05 - Settings: about:preferences#appearance
    settingsAppearance: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, "about:preferences#appearance");
      },
    },

    // B06 - Credential management: about:logins
    aboutLogins: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, "about:logins");
      },
    },

    // B07 - Sidebar: Synced tabs panel
    sidebarSyncedTabs: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await win.SidebarController.show("viewTabsSidebar");
      },
    },

    // B09 - Horizontal tabs: 12+ tabs, horizontal layout
    horizontalTabsMany: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        loadBackgroundTabs(win, 12);
      },
    },

    // Optional
    // C03 - AI side panel open
    sidePanelAI: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await win.SidebarController.show("viewGenaiChatSidebar");
      },
    },

    // C05 - High contrast mode
    hcm: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        Services.prefs.setBoolPref("ui.useAccessibilityTheme", true);
      },
    },

    // C06 - Compact density, horizontal tabs
    compactDensity: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        win.gCustomizeMode.setUIDensity(win.gUIDensity.MODE_COMPACT);
      },
    },

    // C08 - Fullscreen
    fullscreen: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await toggleFullScreen(win, true);
        await settle();
      },
    },

    // Edge cases
    // E01 - Window maximized, horizontal tabs
    chromeMaximized: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        win.maximize();
        await settle();
      },
    },

    // E02 - Window ~1024px wide, tab strip compression
    chromeNarrow: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        win.resizeTo(1024, win.outerHeight);
        await settle();
      },
    },

    // E03 - Horizontal tabs, strip overflow / scroll
    horizontalTabsOverflow: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        loadBackgroundTabs(win, 25);
      },
    },

    // E04 - Pinned tabs row, vertical tabs
    pinnedTabsVertical: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await enableVerticalTabs(win);
        win.gBrowser.pinTab(win.gBrowser.selectedTab);
      },
    },

    // E05 - 2+ tab groups, one collapsed, one expanded
    multipleTabGroups: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        // Built from fresh background tabs rather than addTabGroup()'s
        // selected-tab anchor, so the active tab stays outside both groups:
        // addTabGroup() would otherwise move it from the first group into
        // the second, since the second call re-reads gBrowser.selectedTab.
        win.gBrowser.addTabGroup(createBackgroundTabs(win, 2), {
          label: "QA",
          color: "blue",
        });
        win.gBrowser.addTabGroup(createBackgroundTabs(win, 2), {
          label: "Docs",
          color: "blue",
        }).collapsed = true;
      },
    },

    // E09 - Sidebar launcher only, panel closed
    sidebarCollapsed: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        win.SidebarController._state.launcherExpanded = false;
      },
    },

    // E10 - Sidebar docked right
    sidebarRight: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        Services.prefs.setBoolPref("sidebar.position_start", false);
        await win.SidebarController.show("viewBookmarksSidebar");
      },
    },

    // E12 - Tab group collapsed in vertical layout
    verticalTabGroupCollapsed: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await enableVerticalTabs(win);
        addTabGroup(win, 3, false).collapsed = true;
      },
    },

    // E15 - Extensions panel open
    extensionsPanel: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        let button = win.document.getElementById("unified-extensions-button");
        let popupShown = lazy.BrowserTestUtils.waitForEvent(
          win,
          "popupshown",
          true,
          e => e.target.id == "unified-extensions-panel"
        );
        button.click();
        await popupShown;
      },
    },

    // E17 - Urlbar query with no suggestions
    urlbarZeroResults: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        win.gURLBar.search(
          "zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz-no-suggestions"
        );
      },
    },

    // E19 - Find bar open over content
    findInPage: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        await win.gLazyFindCommand("open");
      },
    },

    // E25 - Appearance: System theme (auto)
    systemThemeAuto: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        let addon = await lazy.AddonManager.getAddonByID(
          "default-theme@mozilla.org"
        );
        await addon.enable();
      },
    },

    // E26 - OS high contrast / forced colors
    forcedColors: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        Services.prefs.setBoolPref("ui.useAccessibilityTheme", true);
      },
    },

    // E27 - Increased contrast preference
    increasedContrast: {
      selectors: [":root"],
      async applyConfig() {
        let win = await reset();
        await navigateTo(win, HOME_PAGE);
        // 2 = "never" use document colors, i.e. always use Firefox's own
        // high-contrast palette; distinct from OS-level forced colors (E26).
        Services.prefs.setIntPref("browser.display.document_color_use", 2);
      },
    },
  },
};

async function navigateTo(win, url) {
  lazy.BrowserTestUtils.startLoadingURIString(
    win.gBrowser.selectedBrowser,
    url
  );
  await lazy.BrowserTestUtils.browserLoaded(win.gBrowser.selectedBrowser);
}

// Uses Window.synthesizeMouseEvent() directly rather than EventUtils.js,
// since this module doesn't run in a mochitest scope that has it injected.
function hover(element) {
  let rect = element.getBoundingClientRect();
  let elWin = element.documentGlobal;
  let x = rect.left + rect.width / 2;
  let y = rect.top + rect.height / 2;
  elWin.synthesizeMouseEvent("mouseover", x, y);
  elWin.synthesizeMouseEvent("mousemove", x, y);
}

function addTabGroup(win, tabCount, background, label = "QA") {
  let tabs = [win.gBrowser.selectedTab];
  for (let i = 1; i < tabCount; i++) {
    tabs.push(
      win.gBrowser.addTab(HOME_PAGE, {
        inBackground: background,
        triggeringPrincipal: PRINCIPAL,
      })
    );
  }
  return win.gBrowser.addTabGroup(tabs, { label, color: "blue" });
}

function createBackgroundTabs(win, count) {
  return Array.from({ length: count }, () =>
    win.gBrowser.addTab(HOME_PAGE, {
      inBackground: true,
      triggeringPrincipal: PRINCIPAL,
    })
  );
}

// Flipping this pref reflows the whole chrome (tabstrip orientation, sidebar
// launcher, toolbar layout); wait for it to settle before screenshotting.
async function enableVerticalTabs(win) {
  Services.prefs.setBoolPref("sidebar.verticalTabs", true);
  await lazy.SidebarTestUtils.waitForTabstripOrientation(win, "vertical");
}

function loadBackgroundTabs(win, count) {
  let urls = Array.from({ length: count }, () => HOME_PAGE);
  win.gBrowser.loadTabs(urls, {
    inBackground: true,
    triggeringPrincipal: PRINCIPAL,
  });
}

// A single batch isn't guaranteed to overflow the strip (e.g. 15 vertical
// tabs may fit within a tall window), so keep adding tabs in batches until
// it does. arrowscrollbox.js dispatches "overflow" as an untrusted event,
// which BrowserTestUtils.waitForEvent ignores by default, so poll instead.
async function loadTabsUntilOverflow(win, batchSize) {
  let arrowScrollbox = win.gBrowser.tabContainer.arrowScrollbox;
  while (!arrowScrollbox.hasAttribute("overflowing")) {
    loadBackgroundTabs(win, batchSize);
    await settle();
  }
}

async function openAudioTab(win, { background }) {
  let tab = win.gBrowser.addTab(AUDIO_PAGE, {
    inBackground: background,
    triggeringPrincipal: PRINCIPAL,
  });
  await lazy.BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  await TestUtils.waitForCondition(
    () => tab.hasAttribute("soundplaying"),
    "waiting for the audio tab to start playing"
  );
  return tab;
}

async function enterCustomizeMode(win) {
  win.gCustomizeMode.enter();
  await TestUtils.waitForCondition(
    () => win.document.documentElement.hasAttribute("customizing"),
    "waiting for customize mode"
  );
}

function toggleFullScreen(win, wantsFS) {
  win.fullScreen = wantsFS;
  return TestUtils.waitForCondition(
    () => wantsFS == win.document.documentElement.hasAttribute("inFullscreen"),
    "waiting for @inFullscreen change"
  );
}

// Some window-state transitions (maximize, resize) have no completion event;
// this mirrors the fixed wait used by the WindowSize configuration set.
function settle() {
  return new Promise(resolve => setTimeout(resolve, 1000));
}

async function reset() {
  let win = Services.wm.getMostRecentWindow("navigator:browser");

  // The privateWindow config leaves a private window open for its
  // screenshot; close it before falling back to the normal window.
  if (lazy.PrivateBrowsingUtils.isWindowPrivate(win)) {
    await lazy.BrowserTestUtils.closeWindow(win);
    win = Services.wm.getMostRecentWindow("navigator:browser");
  }

  // Exit customize mode if active
  if (win.document.documentElement.hasAttribute("customizing")) {
    win.gCustomizeMode.exit();
    await TestUtils.waitForCondition(
      () => !win.document.documentElement.hasAttribute("customizing"),
      "waiting to exit customize mode"
    );
  }

  win.PanelUI.hide();

  // Close sidebar. The launcher's expanded state depends on tab orientation
  // and sidebar.visibility, and is reconciled automatically when those are
  // reset below, so it isn't touched here.
  if (win.SidebarController.isOpen) {
    win.SidebarController.hide();
  }

  // Blur the urlbar, which also closes its view. Leaving it focused keeps a
  // caret blinking after TestRunner re-enables blinking at cleanup.
  win.gURLBar.blur();

  // Close the find bar; it focuses its search box, leaving a blinking caret.
  if (win.gFindBarInitialized) {
    win.gFindBar.close();
  }

  // Restore window state. Fullscreen must be exited before maximizing or
  // resizing, or the transition doesn't take effect.
  if (win.fullScreen) {
    await toggleFullScreen(win, false);
    await settle();
  }
  win.restore();
  if (win.outerWidth != 1280 || win.outerHeight != 1040) {
    win.resizeTo(1280, 1040);
    await settle();
  }

  // Reset the tab orientation pref first and wait for the reflow it
  // triggers before touching anything else.
  if (Services.prefs.prefHasUserValue("sidebar.verticalTabs")) {
    Services.prefs.clearUserPref("sidebar.verticalTabs");
    await lazy.SidebarTestUtils.waitForTabstripOrientation(win, "horizontal");
  }

  // Reset the remaining prefs touched by our configurations
  for (const pref of [
    "sidebar.position_start",
    "ui.useAccessibilityTheme",
    "browser.display.document_color_use",
    "browser.aboutwelcome.enabled",
    "browser.smartwindow.enabled",
  ]) {
    Services.prefs.clearUserPref(pref);
  }

  // Ungroup any tab groups from the previous scenario so the surviving tab
  // isn't left inside a leftover group.
  for (let group of Array.from(win.gBrowser.tabGroups)) {
    group.ungroupTabs();
  }

  // Close all tabs except one
  let gBrowser = win.gBrowser;
  while (gBrowser.tabs.length > 1) {
    gBrowser.removeTab(gBrowser.tabs[gBrowser.tabs.length - 1], {
      animate: false,
    });
  }
  if (gBrowser.selectedTab.pinned) {
    gBrowser.unpinTab(gBrowser.selectedTab);
  }

  return win;
}
