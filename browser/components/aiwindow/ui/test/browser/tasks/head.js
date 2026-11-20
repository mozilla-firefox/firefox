/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

const WIDGET_ID = "smartwindow-monitor-button";
const PANEL_ID = "smartwindow-monitor-panel";
const TEST_REGION = "US";
const TEST_MARKETS = JSON.stringify([[TEST_REGION, ["en-*"]]]);

const { MONITOR_CONDITION_MET_TOPIC, MONITOR_RUN_FAILED_TOPIC } =
  ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs"
  );

/**
 * Turns the monitor button on for the rest of the file, with the home region
 * pinned to a supported market.
 */
async function setupMonitorButton() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.smartwindow.agent.enabled", true],
      ["browser.smartwindow.agent.toolbar.enabled", true],
      ["browser.smartwindow.agent.supportedRegions", TEST_MARKETS],
    ],
  });

  // Pin the home region, otherwise the region gate decides whether the button
  // exists at all and every test here becomes a coin toss.
  const originalRegion = Region.home;
  Region._setHomeRegion(TEST_REGION, false);
  AIWindowToolbarButtons.updateMonitorRegistration();

  registerCleanupFunction(() => {
    Region._setHomeRegion(originalRegion, false);
    AIWindowToolbarButtons.updateMonitorRegistration();
  });
}

function getMonitorButton(win) {
  return win.document.getElementById(WIDGET_ID);
}

function notifyMatch(monitorId) {
  Services.obs.notifyObservers(null, MONITOR_CONDITION_MET_TOPIC, monitorId);
}

function notifyRunFailed(monitorId) {
  Services.obs.notifyObservers(null, MONITOR_RUN_FAILED_TOPIC, monitorId);
}

/**
 * Clicks the monitor button and waits for the panel it opens.
 *
 * @param {ChromeWindow} win
 * @param {number} [monitorCount] - Also wait until the panel lists this many
 *   monitors.
 * @returns {Promise<{panel: XULPopupElement, contents: HTMLElement}>} The
 *   panel and its agent-monitor-panel element.
 */
async function openMonitorPanel(win, monitorCount) {
  const shown = BrowserTestUtils.waitForEvent(
    win.document.getElementById("mainPopupSet"),
    "popupshown"
  );
  EventUtils.synthesizeMouseAtCenter(getMonitorButton(win), {}, win);
  const panel = (await shown).target;
  const contents = panel.querySelector("agent-monitor-panel");
  if (monitorCount !== undefined) {
    await TestUtils.waitForCondition(
      () => contents.monitors.length === monitorCount,
      `Waiting for the panel to list ${monitorCount} monitors`
    );
  }
  await contents.updateComplete;
  return { panel, contents };
}

/**
 * Hides the monitor panel if it is showing and waits for it to leave the DOM.
 *
 * @param {ChromeWindow} win
 */
async function closeMonitorPanel(win) {
  const panel = win.document.getElementById(PANEL_ID);
  if (!panel) {
    return;
  }
  panel.hidePopup();
  await TestUtils.waitForCondition(
    () => !win.document.getElementById(PANEL_ID),
    "Waiting for the monitor panel to be removed"
  );
}
