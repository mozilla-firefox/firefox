/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Tests the Search Tabs keyboard shortcut (Ctrl+Shift+A / Cmd+Shift+A)
 * triggers Address Bar search mode for tabs.
 */
ChromeUtils.defineLazyGetter(this, "UrlbarTestUtils", () => {
  const { UrlbarTestUtils: module } = ChromeUtils.importESModule(
    "resource://testing-common/UrlbarTestUtils.sys.mjs"
  );
  module.init(this);
  return module;
});

add_setup(async function () {
  registerCleanupFunction(resetUrlbar);
});

// Test that the shortcut works with multiple tabs open.
add_task(async function testSearchTabsShortcutWithMultipleTabs() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:mozilla");
  try {
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
    await triggerSearchTabsShortcutAndVerify();

    await UrlbarTestUtils.promisePopupOpen(window, () => {
      EventUtils.sendString("mozilla", window);
    });
    await UrlbarTestUtils.promiseSearchComplete(window);

    let heuristic = await UrlbarTestUtils.getDetailsOfResultAt(window, 0);
    ok(heuristic.heuristic, "The first result is the heuristic.");
    let result = await UrlbarTestUtils.getDetailsOfResultAt(window, 1);
    is(result.url, "about:mozilla", "The matching tab is returned.");
    is(
      result.type,
      UrlbarShared.RESULT_TYPE.TAB_SWITCH,
      "The result switches to the matching tab."
    );
  } finally {
    await resetUrlbar();
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function testSearchTabsShortcutFromPopup() {
  let popup = await BrowserTestUtils.openNewBrowserWindow({
    all: false,
    features: "resizable,width=500,height=500",
  });
  try {
    ok(!popup.toolbar.visible, "The test window is a popup.");
    let originalValue = popup.gURLBar.value;
    popup.document.getElementById("Browser:SearchTabs").doCommand();
    await UrlbarTestUtils.assertSearchMode(window, {
      source: gURLBar.searchModeForToken("%").source,
      entry: "shortcut",
    });
    ok(gURLBar.focused, "The normal window's URL bar is focused.");
    is(popup.gURLBar.value, originalValue, "The popup URL is unchanged.");
    ok(!popup.gURLBar.searchMode, "The popup does not enter search mode.");
  } finally {
    await BrowserTestUtils.closeWindow(popup);
    await resetUrlbar();
  }
});

add_task(async function testSearchTabsOpensNewWindow() {
  let newWindowPromise = TestUtils.topicObserved(
    "browser-delayed-startup-finished"
  );
  BrowserWindowTracker.untrackForTestsOnly(window);
  try {
    document.getElementById("Browser:SearchTabs").doCommand();
  } finally {
    BrowserWindowTracker.track(window);
  }
  let [newWindow] = await newWindowPromise;
  try {
    await UrlbarTestUtils.assertSearchMode(newWindow, {
      source: newWindow.gURLBar.searchModeForToken("%").source,
      entry: "shortcut",
    });
    ok(
      newWindow.gURLBar.hasAttribute("focused"),
      "The new URL bar is focused."
    );
  } finally {
    await BrowserTestUtils.closeWindow(newWindow);
  }
});

async function resetUrlbar() {
  if (gURLBar.searchMode) {
    await UrlbarTestUtils.exitSearchMode(window, { waitForSearch: false });
  }
  await UrlbarTestUtils.promisePopupClose(window);
  gURLBar.handleRevert();
  gURLBar.blur();
}

async function triggerSearchTabsShortcutAndVerify() {
  EventUtils.synthesizeKey("a", { shiftKey: true, accelKey: true });
  let expectedSearchMode = {
    source: gURLBar.searchModeForToken("%").source,
    entry: "shortcut",
  };
  await UrlbarTestUtils.assertSearchMode(window, expectedSearchMode);
  ok(gURLBar.hasAttribute("focused"), "The URL bar is focused.");
}
