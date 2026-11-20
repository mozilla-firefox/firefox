"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["dom.fullscreen.keyboard_lock.enabled", true]],
  });
});

// This is needed now because we request replies, so we must first queue "something" in the content process
// as well as timeout ourselves here.
async function synthesizeKeyRoundtripFlush(tab, key, options) {
  EventUtils.synthesizeKey(key, options);
  await SpecialPowers.spawn(tab.linkedBrowser, [], () => {});
  await new Promise(resolve => setTimeout(resolve, 0));
}

/**
 * Verifies Ctrl+Tab and Ctrl+Shift+Tab behaviour with fullscreen keyboard lock.
 *
 * With keyboard lock active  → both combos must not switch tabs.
 * After exiting fullscreen   → Ctrl+Tab must switch tabs again.
 *
 * @param {boolean} sortByRecentlyUsed
 *   When false, tabbox.js handles Ctrl+Tab (default).
 *   When true,  browser-ctrlTab.mjs shows a panel and commits on Ctrl keyup.
 */
async function runCtrlTabTest(sortByRecentlyUsed) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ctrlTab.sortByRecentlyUsed", sortByRecentlyUsed]],
  });

  const tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  const tab2 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );

  // tab2 was opened last, so it's "next" in both tab-strip and MRU order.
  gBrowser.selectedTab = tab1;

  await SpecialPowers.spawn(tab1.linkedBrowser, [], () => {
    content.document.addEventListener("keydown", e => e.preventDefault(), true);
  });

  await DOMFullscreenTestUtils.changeFullscreen(tab1.linkedBrowser, true, {
    keyboardLock: "browser",
  });

  // --- keyboard lock active ---

  await synthesizeKeyRoundtripFlush(tab1, "KEY_Tab", { ctrlKey: true });

  is(
    gBrowser.selectedTab,
    tab1,
    "Ctrl+Tab must not switch tabs while fullscreen keyboard lock is active"
  );

  // Release Ctrl to dismiss any stray panel if the assertion above ever fails.
  await synthesizeKeyRoundtripFlush(tab1, "VK_CONTROL", { type: "keyup" });
  await synthesizeKeyRoundtripFlush(tab1, "KEY_Tab", {
    ctrlKey: true,
    shiftKey: true,
  });

  is(
    gBrowser.selectedTab,
    tab1,
    "Ctrl+Shift+Tab must not switch tabs while fullscreen keyboard lock is active"
  );
  await synthesizeKeyRoundtripFlush(tab1, "VK_CONTROL", { type: "keyup" });

  // --- exit fullscreen ---

  await DOMFullscreenTestUtils.changeFullscreen(tab1.linkedBrowser, false);

  // After exiting fullscreen the shortcut must work again. For the
  // sortByRecentlyUsed=true path the ctrlTab panel opens on Ctrl+Tab and
  // commits the selection on Ctrl keyup, so we release Ctrl before asserting.
  await synthesizeKeyRoundtripFlush(tab1, "KEY_Tab", { ctrlKey: true });
  await synthesizeKeyRoundtripFlush(tab1, "VK_CONTROL", { type: "keyup" });
  isnot(
    gBrowser.selectedTab,
    tab1,
    "Ctrl+Tab must switch tabs after exiting fullscreen"
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  await SpecialPowers.popPrefEnv();
}

add_task(async function test_ctrltab_keyboard_lock_tabbox_handler() {
  await runCtrlTabTest(false);
});

add_task(async function test_ctrltab_keyboard_lock_ctrlTab_panel_handler() {
  await runCtrlTabTest(true);
});

// Regression guard: fullscreen *without* keyboard lock must not block Ctrl+Tab.
add_task(async function test_ctrltab_fullscreen_without_keyboard_lock() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ctrlTab.sortByRecentlyUsed", false]],
  });

  const tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  const tab2 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com"
  );
  gBrowser.selectedTab = tab1;

  await DOMFullscreenTestUtils.changeFullscreen(tab1.linkedBrowser, true, {
    keyboardLock: "none",
  });

  await synthesizeKeyRoundtripFlush(tab1, "KEY_Tab", { ctrlKey: true });
  await synthesizeKeyRoundtripFlush(tab1, "VK_CONTROL", { type: "keyup" });
  isnot(
    gBrowser.selectedTab,
    tab1,
    "Ctrl+Tab must still switch tabs in fullscreen when keyboard lock is not active"
  );

  // Switching away from a fullscreen tab exits it; wait for that if needed.
  if (document.fullscreenElement) {
    await BrowserTestUtils.waitForEvent(
      document,
      "fullscreenchange",
      false,
      () => !document.fullscreenElement
    );
  }

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  await SpecialPowers.popPrefEnv();
});
