/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ScreenshotsUtils:
    "moz-src:///browser/components/screenshots/ScreenshotsUtils.sys.mjs",
});

const { SELECTION_MODES } = ChromeUtils.importESModule(
  "moz-src:///browser/components/screenshots/ScreenshotsSelectionModes.sys.mjs"
);

/**
 * The single mini_window.created event recorded since the last reset.
 *
 * @returns {object|undefined} its extras.
 */
function createdEvent() {
  let events = Glean.miniWindow.created.testGetValue() ?? [];
  Assert.equal(events.length, 1, "exactly one created event was recorded");
  return events[0]?.extra;
}

add_setup(async () => {
  Services.fog.testResetFOG();
});

add_task(async function test_full_tab_creation_is_recorded() {
  Services.fog.testResetFOG();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  let opened = BrowserTestUtils.domWindowOpenedAndLoaded(null);
  let popPromise = MiniWindowManager.popTab(tab, "tab_context_menu");
  let miniWin = await opened;
  let popup = await popPromise;

  Assert.deepEqual(
    createdEvent(),
    {
      type: "full_tab",
      entry_point: "tab_context_menu",
      was_last_tab: "false",
      concurrent_open: "1",
    },
    "a full-tab mini window reports its flavour and entry point"
  );

  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs();
});

add_task(async function test_cropped_creation_is_recorded() {
  Services.fog.testResetFOG();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );
  let browser = tab.linkedBrowser;

  let miniWin = await popTabForTest(tab, {
    left: 0,
    top: 0,
    width: 300,
    height: 220,
    viewportWidth: browser.clientWidth,
    viewportHeight: browser.clientHeight,
    fullZoom: 1,
  });
  let popup = [...MiniWindowManager._miniwindows][0];

  let extra = createdEvent();
  Assert.equal(extra.type, "fragment", "a framed region reports as fragment");
  Assert.equal(extra.concurrent_open, "1", "counts this mini window");

  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);
  assertNoMiniWindowsOpen();
  removeTestTabs();
});

add_task(async function test_concurrent_open_counts_every_mini_window() {
  Services.fog.testResetFOG();
  let tabs = [];
  for (let i = 0; i < 2; i++) {
    tabs.push(
      await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        `https://example.com/?telemetry=${i}`
      )
    );
  }

  let popups = [];
  for (let tab of tabs) {
    let opened = BrowserTestUtils.domWindowOpenedAndLoaded(null);
    let popPromise = MiniWindowManager.popTab(tab, "tab_context_menu");
    await opened;
    popups.push(await popPromise);
  }

  let counts = Glean.miniWindow.created
    .testGetValue()
    .map(e => e.extra.concurrent_open);
  Assert.deepEqual(counts, ["1", "2"], "each creation reports the live count");

  for (let popup of popups) {
    popup.close();
  }

  await waitForNoMiniWindowsOpen();
  removeTestTabs();
});

add_task(async function test_close_button_is_recorded() {
  Services.fog.testResetFOG();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  let opened = BrowserTestUtils.domWindowOpenedAndLoaded(null);
  let popPromise = MiniWindowManager.popTab(tab, "tab_context_menu");
  let miniWin = await opened;
  let popup = await popPromise;

  popup.close();
  await BrowserTestUtils.domWindowClosed(miniWin);

  let events = Glean.miniWindow.closed.testGetValue() ?? [];
  Assert.equal(events.length, 1, "one closed event was recorded");
  let extra = events[0].extra;
  Assert.equal(extra.type, "full_tab", "reports the flavour");
  Assert.equal(extra.method, "close_button", "reports how it ended");
  Assert.greaterOrEqual(
    parseInt(extra.duration_ms),
    0,
    "reports a duration in ms"
  );

  let distribution = Glean.miniWindow.openDuration.full_tab.testGetValue();
  Assert.equal(distribution.count, 1, "one open_duration sample was recorded");

  assertNoMiniWindowsOpen();
  removeTestTabs();
});

add_task(async function test_put_tab_back_is_recorded() {
  Services.fog.testResetFOG();
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  let opened = BrowserTestUtils.domWindowOpenedAndLoaded(null);
  let popPromise = MiniWindowManager.popTab(tab, "tab_context_menu");
  let miniWin = await opened;
  let popup = await popPromise;

  popup.returnToOriginWin(true);
  await BrowserTestUtils.domWindowClosed(miniWin);

  let extra = Glean.miniWindow.closed.testGetValue()[0].extra;
  Assert.equal(extra.method, "put_tab_back", "restoring reports put_tab_back");

  assertNoMiniWindowsOpen();
  removeTestTabs();
});

/**
 * Run a real Screenshots mini-window session: start it the way `reason`
 * starts it, then hand the parent actor the selection message the overlay
 * sends. The handler exits Screenshots before popping, so this is the path
 * where the entry point has to survive exit() clearing per-browser state.
 *
 * @param {XULBrowserElement} browser
 * @param {string} reason - Screenshots' own name for the entry point.
 */
async function popViaScreenshots(browser, reason) {
  ScreenshotsUtils.toggle(browser, reason, {
    mode: SELECTION_MODES.MINI_WINDOW,
  });

  // receiveMessage lives on the ScreenshotsComponentParent actor, and reads
  // the browser back off the message's target.
  let actor = ScreenshotsUtils.getActor(browser);
  let opened = BrowserTestUtils.domWindowOpenedAndLoaded(null);
  await actor.receiveMessage({
    name: "Screenshots:MiniWindowCropSelection",
    data: {
      region: { left: 10, top: 20, width: 200, height: 150 },
      viewportWidth: browser.clientWidth,
      viewportHeight: browser.clientHeight,
    },
    target: actor,
  });
  return opened;
}

add_task(async function test_screenshots_entry_points_are_recorded() {
  for (let [reason, expected] of [
    ["MiniWindowToolbarButton", "toolbar_button"],
    ["MiniWindowContextMenu", "content_context_menu"],
  ]) {
    Services.fog.testResetFOG();
    let tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      "https://example.com/"
    );

    let miniWin = await popViaScreenshots(tab.linkedBrowser, reason);
    let extra = Glean.miniWindow.created.testGetValue()[0].extra;
    Assert.equal(extra.entry_point, expected, `${reason} reports ${expected}`);
    Assert.equal(extra.type, "fragment", "a framed region reports as fragment");

    let popup = [...MiniWindowManager._miniwindows][0];
    popup.close();
    await BrowserTestUtils.domWindowClosed(miniWin);
    assertNoMiniWindowsOpen();
    removeTestTabs();
  }
});
