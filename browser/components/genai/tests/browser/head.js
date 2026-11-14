/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

ChromeUtils.defineLazyGetter(this, "SidebarTestUtils", () => {
  const { SidebarTestUtils: utils } = ChromeUtils.importESModule(
    "resource://testing-common/SidebarTestUtils.sys.mjs"
  );
  utils.init(this);
  return utils;
});

/**
 * Select all the text of a page with the mouse pressed, then release it.
 *
 * @param {MozBrowser} browser the browser to select in
 * @param {object} [options]
 * @param {Window} [options.win] the window to run the select all command in
 * @param {number} [options.releaseX] where to release the mouse
 * @param {number} [options.releaseY] where to release the mouse
 * @param {number} [options.holdMs] ms to wait after selecting before the release
 */
async function selectAllAndMouseUp(
  browser,
  { win = window, releaseX = 60, releaseY = 10, holdMs = 0 } = {}
) {
  await SimpleTest.promiseFocus(browser);
  await BrowserTestUtils.synthesizeMouse(
    browser,
    10,
    10,
    { type: "mousedown" },
    browser
  );
  const selectPromise = SpecialPowers.spawn(browser, [], () =>
    ContentTaskUtils.waitForCondition(() => content.getSelection().toString())
  );
  win.goDoCommand("cmd_selectAll");
  await selectPromise;
  if (holdMs) {
    // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
    await new Promise(resolve => setTimeout(resolve, holdMs));
  }
  await BrowserTestUtils.synthesizeMouse(
    browser,
    releaseX,
    releaseY,
    { type: "mouseup" },
    browser
  );
}

function visibleBox(popup) {
  const outer = popup.getOuterScreenRect();
  const client = popup.getBoundingClientRect();
  const content = popup.shadowRoot
    .querySelector("[part=content]")
    .getBoundingClientRect();
  return {
    top: outer.y + content.top - client.top,
    bottom: outer.y + content.bottom - client.top,
    left: outer.x + content.left - client.left,
  };
}

async function showSelectionMenu(browser, options) {
  const panel = document.getElementById("selection-shortcut-action-panel");
  Assert.ok(!panel.hasAttribute("panelopen"), "Menu starts closed");

  await selectAllAndMouseUp(browser, options);
  await TestUtils.waitForCondition(
    () => panel.getAttribute("panelopen") === "true",
    "Selection menu opened"
  );
  return panel;
}

const MORE_ACTIONS_PREFS = [
  ["browser.ml.chat.shortcuts", true],
  ["browser.ml.chat.shortcut.onboardingMouseoverCount", 2],
  ["browser.ml.chat.provider", "http://localhost:8080"],
  ["browser.highlightToSearch.featureGate", true],
  ["browser.highlightToSearch.enabled", true],
  ["browser.highlightToSearch.search.enabled", true],
  ["browser.highlightToSearch.copy.enabled", true],
];

async function openMoreActions() {
  const moreActionsPanel = document.getElementById(
    "selection-more-actions-panel"
  );
  const shown = BrowserTestUtils.waitForEvent(moreActionsPanel, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("more-actions-button"),
    {},
    window
  );
  await shown;
  return moreActionsPanel;
}

async function clickToggle(id) {
  const toggle = document.getElementById(id);
  await toggle.updateComplete;
  EventUtils.synthesizeMouseAtCenter(toggle.buttonEl, {});
  await toggle.updateComplete;
}

const CALLOUT_SEEN_PREF = "browser.highlightToSearch.callout.seen";

function getCalloutPanel() {
  return document.getElementById("selection-callout-panel");
}

function getCalloutState() {
  return getCalloutPanel()?.state ?? "closed";
}

async function waitForCalloutOpen() {
  await TestUtils.waitForCondition(
    () => getCalloutState() == "open",
    "Selection callout opened"
  );
  return getCalloutPanel();
}

async function hideSelectionMenu(panel) {
  const hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  panel.hide();
  await hidden;
}

function contentRect(popup) {
  return popup.shadowRoot
    .querySelector("[part=content]")
    .getBoundingClientRect();
}

async function openAskDropdown(browser) {
  const popup = document.getElementById("chat-shortcuts-options-panel");
  const shown = BrowserTestUtils.waitForEvent(popup, "popupshown");
  EventUtils.synthesizeMouseAtCenter(
    document.getElementById("ai-action-button"),
    {},
    window
  );
  await shown;
  // Let the page hear that a dropdown is open before the next input.
  await SpecialPowers.spawn(browser, [], () => {});
  return popup;
}
