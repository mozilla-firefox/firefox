/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { SessionStoreTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/SessionStoreTestUtils.sys.mjs"
);

const PREF_ID_ALWAYS_ASK =
  "browser.privatebrowsing.resetPBM.showConfirmationDialog";

const SELECTOR_TOOLBAR_BUTTON = "#reset-pbm-toolbar-button";

const SELECTOR_PANELVIEW = "panel #reset-pbm-panel";
const SELECTOR_CONTAINER = "#reset-pbm-panel-container";
const SELECTOR_PANEL_HEADING = "#reset-pbm-panel-header > description";
const SELECTOR_PANEL_DESCRIPTION = "#reset-pbm-panel-description";
const SELECTOR_PANEL_CHECKBOX = "#reset-pbm-panel-checkbox";
const SELECTOR_PANEL_CONFIRM_BTN = "#reset-pbm-panel-confirm-button";
const SELECTOR_PANEL_CANCEL_BTN = "#reset-pbm-panel-cancel-button";

const SELECTOR_PANEL_COMPLETION_TOAST = "#confirmation-hint";

/**
 * Wait for the reset pbm confirmation panel to open. May also be called if the
 * panel is already open.
 *
 * @param {ChromeWindow} win - Chrome window in which the panel is embedded.
 * @returns {Promise} - Promise which resolves once the panel has been shown.
 * Resolves directly if the panel is already visible.
 */
async function waitForConfirmPanelShow(win) {
  // Check for the panel, if it's not present yet wait for it to be inserted.
  let panelview = win.document.querySelector(SELECTOR_PANELVIEW);
  // Panel already visible, we can exit early.
  if (panelview && BrowserTestUtils.isVisible(panelview)) {
    return;
  }

  // Wait for panel shown event.
  await BrowserTestUtils.waitForEvent(win, "popupshown", event => {
    return event.target.querySelector(SELECTOR_PANELVIEW);
  });
}

/**
 * Hides the completion toast which is shown after the reset action has been
 * completed.
 *
 * @param {ChromeWindow} win - Chrome window the toast is shown in.
 */
async function hideCompletionToast(win) {
  let promiseHidden = BrowserTestUtils.waitForEvent(
    win.ConfirmationHint._panel,
    "popuphidden"
  );

  win.ConfirmationHint._panel.hidePopup();

  await promiseHidden;
}

/**
 * Trigger the reset pbm toolbar button which may open the confirm panel in the
 * given window.
 *
 * @param {nsIDOMWindow} win - PBM window to trigger the button in.
 * @param {boolean} [expectPanelOpen] - After the button action: whether the
 * panel is expected to open (true) or remain closed (false).
 * @returns {Promise} - Promise which resolves once the button has been
 * triggered and, if applicable, the panel has been opened.
 */
async function triggerResetBtn(win, expectPanelOpen = true) {
  Assert.ok(
    PrivateBrowsingUtils.isWindowPrivate(win),
    "Window to open panel is in PBM."
  );

  let shownPromise;
  if (expectPanelOpen) {
    shownPromise = waitForConfirmPanelShow(win);
  }

  await BrowserTestUtils.synthesizeMouseAtCenter(
    SELECTOR_TOOLBAR_BUTTON,
    {},
    win.browsingContext
  );

  // Promise may not be defined at this point in which case this is a no-op.
  await shownPromise;
}

/**
 * Provides a promise that resolves once the reset confirmation panel has been hidden.
 *
 * @param nsIDOMWindow win - Chrome window that has the panel.
 * @returns {Promise}
 */
function waitForConfirmPanelHidden(win) {
  return BrowserTestUtils.waitForEvent(
    win.document.querySelector(SELECTOR_PANELVIEW).closest("panel"),
    "popuphidden"
  );
}

/**
 * Provides a promise that resolves once the completion toast has been shown.
 *
 * @param nsIDOMWindow win - Chrome window that has the panel.
 * @returns {Promise}
 */
function waitForCompletionToastShown(win) {
  // Init the confirmation hint panel so we can listen for show events.
  win.ConfirmationHint._ensurePanel();
  return BrowserTestUtils.waitForEvent(
    win.document.querySelector(SELECTOR_PANEL_COMPLETION_TOAST),
    "popupshown"
  );
}

/**
 * Wait for private browsing data clearing to be triggered.
 * Clearing is not guaranteed to be done at this point. Bug 1846494 will add a
 * promise based mechanism and potentially a new triggering method for clearing,
 * at which point this helper should be updated.
 *
 * @returns {Promise} Promise which resolves when the last-pb-context-exited
 * message has been dispatched.
 */
function waitForPBMDataClear() {
  return TestUtils.topicObserved("last-pb-context-exited");
}

/**
 * Test panel visibility.
 *
 * @param {nsIDOMWindow} win - Chrome window which is the parent of the panel.
 * @param {string} selector - Query selector for the panel.
 * @param {boolean} expectVisible - Whether the panel should be visible (true) or invisible or not present (false).
 */
function assertPanelVisibility(win, selector, expectVisible) {
  let panelview = win.document.querySelector(selector);

  if (expectVisible) {
    Assert.ok(panelview, `Panelview element ${selector} should exist.`);
    Assert.ok(
      BrowserTestUtils.isVisible(panelview),
      `Panelview ${selector} should be visible.`
    );
    return;
  }

  Assert.ok(
    !panelview || !BrowserTestUtils.isVisible(panelview),
    `Panelview ${selector} should be invisible or non-existent.`
  );
}

function transformGleanEvents(events) {
  if (!events) {
    return [];
  }
  return events.map(e => ({ ...e.extra }));
}

function assertTelemetry(expectedConfirmPanel, expectedResetAction, message) {
  info(message);

  let confirmPanelEvents = transformGleanEvents(
    Glean.privateBrowsingResetPbm.confirmPanel.testGetValue()
  );
  Assert.deepEqual(
    confirmPanelEvents,
    expectedConfirmPanel,
    "confirmPanel events should match."
  );

  let resetActionEvents = transformGleanEvents(
    Glean.privateBrowsingResetPbm.resetAction.testGetValue()
  );
  Assert.deepEqual(
    resetActionEvents,
    expectedResetAction,
    "resetAction events should match."
  );
}

/**
 * Tests that the reset button is only visible in private browsing windows and
 * when the feature is enabled.
 */
add_task(async function test_toolbar_button_visibility() {
  assertTelemetry([], [], "No glean events initially.");

  for (let isEnabled of [false, true]) {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.privatebrowsing.resetPBM.enabled", isEnabled]],
    });

    info(
      "Test that the toolbar button does not exist in a normal browsing window."
    );
    let toolbarButtonNormalBrowsing = document.querySelector(
      SELECTOR_TOOLBAR_BUTTON
    );
    Assert.equal(
      !!toolbarButtonNormalBrowsing,
      false,
      "Normal browsing toolbar button element does not exist."
    );

    info(
      "Test that the toolbar button is visible in a private browsing window, depending on enabled pref state."
    );
    let privateWindow = await BrowserTestUtils.openNewBrowserWindow({
      private: true,
    });

    let toolbarButtonPrivateBrowsing = privateWindow.document.querySelector(
      SELECTOR_TOOLBAR_BUTTON
    );
    Assert.equal(
      !!toolbarButtonPrivateBrowsing,
      isEnabled,
      "Private browsing toolbar button element exists, depending on enabled pref state."
    );
    if (toolbarButtonPrivateBrowsing) {
      Assert.equal(
        BrowserTestUtils.isVisible(toolbarButtonPrivateBrowsing),
        isEnabled,
        "Toolbar button is visible in private browsing if enabled."
      );
    }

    await BrowserTestUtils.closeWindow(privateWindow);

    await SpecialPowers.popPrefEnv();
  }

  assertTelemetry([], [], "No glean events after test.");
});

/**
 * Tests the panel UI, the 'always ask' checkbox and the confirm and cancel
 * actions.
 */
add_task(async function test_panel() {
  assertTelemetry([], [], "No glean events initially.");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.privatebrowsing.resetPBM.enabled", true]],
  });

  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  // Panel does either not exist (because it's lazy and hasn't been opened yet),
  // or it is invisible.
  assertPanelVisibility(privateWin, SELECTOR_PANELVIEW, false);
  assertPanelVisibility(privateWin, SELECTOR_PANEL_COMPLETION_TOAST, false);

  info("Open the panel.");
  await triggerResetBtn(privateWin);

  assertTelemetry(
    [{ action: "show", reason: "toolbar-btn" }],
    [],
    "There should be a panel show event."
  );

  info("Check that all expected elements are present and visible.");
  [
    SELECTOR_PANEL_HEADING,
    SELECTOR_PANEL_DESCRIPTION,
    SELECTOR_PANEL_CHECKBOX,
    SELECTOR_PANEL_CONFIRM_BTN,
    SELECTOR_PANEL_CANCEL_BTN,
  ].forEach(elSelector => {
    let el = privateWin.document.querySelector(elSelector);
    Assert.ok(el, `Panel element ${elSelector} exists.`);
    if (el) {
      Assert.ok(
        BrowserTestUtils.isVisible(el),
        `Panel element ${elSelector} is visible.`
      );
    }
  });

  info("Inspect checkbox and pref state.");
  let checkbox = privateWin.document.querySelector(SELECTOR_PANEL_CHECKBOX);
  Assert.ok(
    checkbox.checked,
    "The 'always ask' checkbox should be checked initially."
  );
  Assert.ok(
    Services.prefs.getBoolPref(
      "browser.privatebrowsing.resetPBM.showConfirmationDialog"
    ),
    "The always ask pref should be true."
  );

  info("Accessibility checks");
  let panel = privateWin.document.querySelector(SELECTOR_PANELVIEW);
  Assert.equal(
    panel.getAttribute("role"),
    "document",
    "Panel should have role document."
  );

  let container = panel.querySelector(SELECTOR_CONTAINER);
  Assert.equal(
    container.getAttribute("role"),
    "alertdialog",
    "Panel container should have role alertdialog."
  );
  Assert.equal(
    container.getAttribute("aria-labelledby"),
    "reset-pbm-panel-header",
    "aria-labelledby should point to heading."
  );

  let heading = panel.querySelector(SELECTOR_PANEL_HEADING);
  Assert.equal(
    heading.getAttribute("role"),
    "heading",
    "Heading should have role heading."
  );
  Assert.equal(
    heading.getAttribute("aria-level"),
    "2",
    "heading should have aria-level 2"
  );

  info("Click the checkbox to uncheck it.");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    SELECTOR_PANEL_CHECKBOX,
    {},
    privateWin.browsingContext
  );
  Assert.ok(
    !checkbox.checked,
    "The 'always ask' checkbox should no longer be checked."
  );
  info(
    "The pref shouldn't update after clicking the checkbox. It only updates on panel confirm."
  );
  Assert.ok(
    Services.prefs.getBoolPref(
      "browser.privatebrowsing.resetPBM.showConfirmationDialog"
    ),
    "The always ask pref should still be true."
  );

  info("Close the panel via cancel.");
  let promisePanelHidden = waitForConfirmPanelHidden(privateWin);
  await BrowserTestUtils.synthesizeMouseAtCenter(
    SELECTOR_PANEL_CANCEL_BTN,
    {},
    privateWin.browsingContext
  );
  await promisePanelHidden;

  assertTelemetry(
    [
      { action: "show", reason: "toolbar-btn" },
      { action: "hide", reason: "cancel-btn" },
    ],
    [],
    "There should be a panel show and a hide event."
  );

  assertPanelVisibility(privateWin, SELECTOR_PANELVIEW, false);
  assertPanelVisibility(privateWin, SELECTOR_PANEL_COMPLETION_TOAST, false);

  info("Reopen the panel.");
  await triggerResetBtn(privateWin);

  assertTelemetry(
    [
      { action: "show", reason: "toolbar-btn" },
      { action: "hide", reason: "cancel-btn" },
      { action: "show", reason: "toolbar-btn" },
    ],
    [],
    "Should have added a show event."
  );

  assertPanelVisibility(privateWin, SELECTOR_PANELVIEW, true);
  assertPanelVisibility(privateWin, SELECTOR_PANEL_COMPLETION_TOAST, false);
  Assert.ok(
    checkbox.checked,
    "The 'always ask' checkbox should be checked again."
  );
  Assert.ok(
    Services.prefs.getBoolPref(PREF_ID_ALWAYS_ASK),
    "The always ask pref should be true."
  );

  info("Test the checkbox on confirm.");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    SELECTOR_PANEL_CHECKBOX,
    {},
    privateWin.browsingContext
  );
  Assert.ok(
    !checkbox.checked,
    "The 'always ask' checkbox should no longer be checked."
  );

  info("Close the panel via confirm.");
  let promiseDataCleared = waitForPBMDataClear();
  promisePanelHidden = waitForConfirmPanelHidden(privateWin);
  let promiseCompletionToastShown = waitForCompletionToastShown(privateWin);

  await BrowserTestUtils.synthesizeMouseAtCenter(
    SELECTOR_PANEL_CONFIRM_BTN,
    {},
    privateWin.browsingContext
  );
  await promisePanelHidden;

  assertTelemetry(
    [
      { action: "show", reason: "toolbar-btn" },
      { action: "hide", reason: "cancel-btn" },
      { action: "show", reason: "toolbar-btn" },
      { action: "hide", reason: "confirm-btn" },
    ],
    [{ did_confirm: "true" }],
    "Should have added a hide and a reset event."
  );
  await promiseCompletionToastShown;
  assertPanelVisibility(privateWin, SELECTOR_PANELVIEW, false);
  assertPanelVisibility(privateWin, SELECTOR_PANEL_COMPLETION_TOAST, true);

  await promiseDataCleared;

  Assert.ok(
    !Services.prefs.getBoolPref(PREF_ID_ALWAYS_ASK),
    "The always ask pref should now be false."
  );

  // Need to hide the completion toast, otherwise the next click on the toolbar
  // button won't work.
  info("Hide the completion toast.");
  await hideCompletionToast(privateWin);

  info(
    "Simulate a click on the toolbar button. This time the panel should not open - we have unchecked 'always ask'."
  );
  promiseDataCleared = waitForPBMDataClear();
  promiseCompletionToastShown = waitForCompletionToastShown(privateWin);

  await triggerResetBtn(privateWin, false);

  info("Waiting for PBM session to end.");
  await promiseDataCleared;
  info("Data has been cleared.");

  assertPanelVisibility(privateWin, SELECTOR_PANELVIEW, false);

  info("Waiting for the completion toast to show.");
  await promiseCompletionToastShown;

  assertPanelVisibility(privateWin, SELECTOR_PANELVIEW, false);
  assertPanelVisibility(privateWin, SELECTOR_PANEL_COMPLETION_TOAST, true);

  assertTelemetry(
    [
      { action: "show", reason: "toolbar-btn" },
      { action: "hide", reason: "cancel-btn" },
      { action: "show", reason: "toolbar-btn" },
      { action: "hide", reason: "confirm-btn" },
    ],
    [{ did_confirm: "true" }, { did_confirm: "false" }],
    "Should have added a reset event."
  );

  await BrowserTestUtils.closeWindow(privateWin);
  Services.prefs.clearUserPref(PREF_ID_ALWAYS_ASK);

  // Clean up telemetry
  Services.fog.testResetFOG();
});

/**
 * Tests that the reset action closes all other private browsing windows and
 * tabs and triggers private browsing data clearing.
 */
add_task(async function test_reset_action() {
  assertTelemetry([], [], "No glean events initially.");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.privatebrowsing.resetPBM.enabled", true]],
  });

  info("Open a few private browsing windows.");
  let privateBrowsingWindows = [];
  for (let i = 0; i < 3; i += 1) {
    privateBrowsingWindows.push(
      await BrowserTestUtils.openNewBrowserWindow({
        private: true,
      })
    );
  }

  info(
    "Open an additional normal browsing window. It should remain open on reset PBM action."
  );
  let additionalNormalWindow = await BrowserTestUtils.openNewBrowserWindow({
    private: false,
  });

  info("Use one of the PBM windows to trigger the PBM restart action.");
  let [win] = privateBrowsingWindows;
  win.focus();

  Assert.ok(
    PrivateBrowsingUtils.isWindowPrivate(win),
    "Window for PBM reset trigger is private window."
  );

  info("Load a bunch of tabs in the private window.");
  let loadPromises = [
    "https://example.com",
    "https://example.org",
    "https://example.net",
  ].map(async url => {
    let tab = BrowserTestUtils.addTab(win.gBrowser, url);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  });
  await Promise.all(loadPromises);

  info("Switch to the last tab.");
  win.gBrowser.selectedTab = win.gBrowser.tabs[win.gBrowser.tabs.length - 1];
  Assert.notEqual(
    win.gBrowser.selectedBrowser.currentURI.spec,
    "about:privatebrowsing",
    "The selected tab should not show about:privatebrowsing."
  );

  let windowClosePromises = [
    ...privateBrowsingWindows.filter(w => win != w),
  ].map(w => BrowserTestUtils.windowClosed(w));

  // Create promises for tab close for all tabs in the triggering private browsing window.
  let promisesTabsClosed = win.gBrowser.tabs.map(tab =>
    BrowserTestUtils.waitForTabClosing(tab)
  );

  info("Trigger the restart PBM action");

  let promiseDataClear = waitForPBMDataClear();
  await ResetPBMPanel._restartPBM(win);

  info(
    "Wait for all the windows but the default normal window and the private window which triggered the reset action to be closed."
  );
  await Promise.all(windowClosePromises);

  info("Wait for tabs in the trigger private window to close.");
  await Promise.all(promisesTabsClosed);

  info("Wait for data to be cleared.");
  await promiseDataClear;

  Assert.equal(
    win.gBrowser.tabs.length,
    1,
    "Should only have 1 tab remaining."
  );

  await TestUtils.waitForCondition(
    () =>
      win.gBrowser.selectedBrowser.currentURI.spec == "about:privatebrowsing"
  );
  Assert.equal(
    win.gBrowser.selectedBrowser.currentURI.spec,
    "about:privatebrowsing",
    "The remaining tab should point to about:privatebrowsing."
  );

  // Close the private window that remained open.
  await BrowserTestUtils.closeWindow(win);
  await BrowserTestUtils.closeWindow(additionalNormalWindow);

  // We bypass telemetry by calling ResetPBMPanel._restartPBM directly.
  assertTelemetry([], [], "No glean events after the test.");
});

/**
 * Ensure that we don't show the tab close warning when closing multiple tabs
 * with the reset PBM action.
 */
add_task(async function test_tab_close_warning_suppressed() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.privatebrowsing.resetPBM.enabled", true],
      ["browser.tabs.warnOnClose", true],
      ["browser.tabs.warnOnCloseOtherTabs", true],
    ],
  });

  info("Open a private browsing window.");
  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  info("Open enough tabs so the tab close warning would show.");
  let loadPromises = [];
  // warnAboutClosingTabs uses this number to determine whether to show the tab
  // close warning.
  let maxTabsUndo = Services.prefs.getIntPref(
    "browser.sessionstore.max_tabs_undo"
  );
  for (let i = 0; i < maxTabsUndo + 2; i++) {
    let tab = BrowserTestUtils.addTab(win.gBrowser, "about:blank");
    loadPromises.push(
      BrowserTestUtils.browserLoaded(tab.linkedBrowser, {
        wantLoad: "about:blank",
      })
    );
  }
  await Promise.all(loadPromises);

  // Create promises for tab close for all tabs in the triggering private browsing window.
  let promisesTabsClosed = win.gBrowser.tabs.map(tab =>
    BrowserTestUtils.waitForTabClosing(tab)
  );

  info("Trigger the restart PBM action");

  let promiseDataClear = waitForPBMDataClear();
  await ResetPBMPanel._restartPBM(win);

  info("Wait for tabs in the trigger private window to close.");
  await Promise.all(promisesTabsClosed);

  info("Wait for data to be cleared.");
  await promiseDataClear;

  Assert.equal(
    win.gBrowser.tabs.length,
    1,
    "Should only have 1 tab remaining."
  );

  await TestUtils.waitForCondition(
    () =>
      win.gBrowser.selectedBrowser.currentURI.spec == "about:privatebrowsing"
  );
  Assert.equal(
    win.gBrowser.selectedBrowser.currentURI.spec,
    "about:privatebrowsing",
    "The remaining tab should point to about:privatebrowsing."
  );

  // Close the private window that remained open.
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Test that if the browser sidebar is open the reset action closes it.
 */
add_task(async function test_reset_action_closes_sidebar() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.privatebrowsing.resetPBM.enabled", true]],
  });

  info("Open a private browsing window.");
  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  info(
    "Open the sidebar of both the private browsing window and the normal browsing window."
  );
  await SidebarController.show("viewBookmarksSidebar");
  await win.SidebarController.show("viewBookmarksSidebar");

  info("Trigger the restart PBM action");
  await ResetPBMPanel._restartPBM(win);

  Assert.ok(
    SidebarController.isOpen,
    "Normal browsing window sidebar should still be open."
  );
  Assert.ok(
    !win.SidebarController.isOpen,
    "Private browsing sidebar should be closed."
  );

  // Cleanup: Close the sidebar of the normal browsing window.
  SidebarController.hide();

  // Cleanup: Close the private window that remained open.
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Test that the session store history gets purged by the reset action.
 */
add_task(async function test_reset_action_purges_session_store() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.privatebrowsing.resetPBM.enabled", true]],
  });

  info("Open a private browsing window.");
  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  Assert.equal(
    SessionStore.getClosedTabCountForWindow(win),
    0,
    "Initially there should be no closed tabs recorded for the PBM window in SessionStore."
  );

  info("Load a bunch of tabs in the private window.");

  let tab;
  let loadPromises = [
    "https://example.com",
    "https://example.org",
    "https://example.net",
  ].map(async url => {
    tab = BrowserTestUtils.addTab(win.gBrowser, url);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  });
  await Promise.all(loadPromises);

  info("Manually close a tab");
  await SessionStoreTestUtils.closeTab(tab);

  Assert.equal(
    SessionStore.getClosedTabCountForWindow(win),
    1,
    "The manually closed tab should be recorded in SessionStore."
  );

  info("Trigger the restart PBM action");
  await ResetPBMPanel._restartPBM(win);

  Assert.equal(
    SessionStore.getClosedTabCountForWindow(win),
    0,
    "After triggering the PBM reset action there should be no closed tabs recorded for the PBM window in SessionStore."
  );

  // Cleanup: Close the private window that remained open.
  await BrowserTestUtils.closeWindow(win);
});

/**
 * Test that the the reset action closes all tabs, including pinned and (multi-)selected
 * tabs.
 */
add_task(async function test_reset_action_closes_pinned_and_selected_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.privatebrowsing.resetPBM.enabled", true]],
  });

  info("Open a private browsing window.");
  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  info("Load a list of tabs.");
  let loadPromises = [
    "https://example.com/",
    "https://example.org/",
    "https://example.net/",
    "about:blank",
  ].map(async url => {
    let tab = BrowserTestUtils.addTab(win.gBrowser, url);
    await BrowserTestUtils.browserLoaded(tab.linkedBrowser, { wantLoad: url });
    return tab;
  });
  let tabs = await Promise.all(loadPromises);

  info("Pin a tab");
  win.gBrowser.pinTab(tabs[0]);

  info("Multi-select tabs.");
  await BrowserTestUtils.switchTab(win.gBrowser, tabs[2]);
  win.gBrowser.addToMultiSelectedTabs(tabs[3]);

  // Create promises for tab close for all tabs in the triggering private browsing window.
  let promisesTabsClosed = win.gBrowser.tabs.map(tab =>
    BrowserTestUtils.waitForTabClosing(tab)
  );

  info("Trigger the restart PBM action");
  await ResetPBMPanel._restartPBM(win);

  info("Wait for all tabs to be closed.");
  await Promise.all(promisesTabsClosed);

  Assert.equal(
    win.gBrowser.tabs.length,
    1,
    "Should only have 1 tab remaining."
  );

  await TestUtils.waitForCondition(
    () =>
      win.gBrowser.selectedBrowser.currentURI.spec == "about:privatebrowsing"
  );
  Assert.equal(
    win.gBrowser.selectedBrowser.currentURI.spec,
    "about:privatebrowsing",
    "The remaining tab should point to about:privatebrowsing."
  );

  // Cleanup: Close the private window/
  await BrowserTestUtils.closeWindow(win);
});
