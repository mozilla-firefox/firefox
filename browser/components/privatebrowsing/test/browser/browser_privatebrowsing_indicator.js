/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const LABELED_INDICATOR_SELECTOR = ".private-browsing-indicator-with-label";
const BUTTON_INDICATOR_SELECTOR = ".private-browsing-indicator-button";

// The private browsing indicator button lives in two possible spots:
// - Inside #TabsToolbar (for horizontal tabs)
// - Inside #nav-bar (for vertical tabs)
//
// The layout tests below make sure at least one copy is always visible in a
// private window, no matter the tabs/titlebar/menubar layout.

add_task(async function test_indicator_horizontal_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", false],
    ],
  });

  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  const tabsToolbarIndicator = win.document.querySelector(
    `#TabsToolbar ${BUTTON_INDICATOR_SELECTOR}`
  );
  const navBarIndicator = win.document.querySelector(
    `#nav-bar ${BUTTON_INDICATOR_SELECTOR}`
  );

  ok(
    BrowserTestUtils.isVisible(tabsToolbarIndicator),
    "Indicator should be visible in TabsToolbar with horizontal tabs"
  );
  ok(
    BrowserTestUtils.isHidden(navBarIndicator),
    "Indicator in nav-bar should be hidden with horizontal tabs"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_indicator_vertical_tabs() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["sidebar.revamp", true],
      ["sidebar.verticalTabs", true],
    ],
  });

  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  const tabsToolbarIndicator = win.document.querySelector(
    `#TabsToolbar ${BUTTON_INDICATOR_SELECTOR}`
  );
  const navBarIndicator = win.document.querySelector(
    `#nav-bar ${BUTTON_INDICATOR_SELECTOR}`
  );

  ok(
    BrowserTestUtils.isHidden(tabsToolbarIndicator),
    "Indicator in TabsToolbar should be hidden with vertical tabs"
  );
  ok(
    BrowserTestUtils.isVisible(navBarIndicator),
    "Indicator should be visible in nav-bar with vertical tabs"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_indicator_tabs_in_titlebar() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.inTitlebar", 1]],
  });

  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  const tabsToolbarIndicator = win.document.querySelector(
    `#TabsToolbar ${BUTTON_INDICATOR_SELECTOR}`
  );
  const navBarIndicator = win.document.querySelector(
    `#nav-bar ${BUTTON_INDICATOR_SELECTOR}`
  );

  ok(
    BrowserTestUtils.isVisible(tabsToolbarIndicator) ||
      BrowserTestUtils.isVisible(navBarIndicator),
    "At least one indicator copy should be visible with tabs in titlebar"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_indicator_with_menubar_shown() {
  CustomizableUI.setToolbarVisibility("toolbar-menubar", true);

  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  const tabsToolbarIndicator = win.document.querySelector(
    `#TabsToolbar ${BUTTON_INDICATOR_SELECTOR}`
  );
  const navBarIndicator = win.document.querySelector(
    `#nav-bar ${BUTTON_INDICATOR_SELECTOR}`
  );

  ok(
    BrowserTestUtils.isVisible(tabsToolbarIndicator) ||
      BrowserTestUtils.isVisible(navBarIndicator),
    "At least one indicator copy should be visible with the menu bar shown"
  );

  await BrowserTestUtils.closeWindow(win);
  CustomizableUI.setToolbarVisibility("toolbar-menubar", false);
});

add_task(async function test_indicator_not_shown_in_normal_window() {
  const win = await BrowserTestUtils.openNewBrowserWindow();

  const indicators = win.document.querySelectorAll(
    `${LABELED_INDICATOR_SELECTOR}, ${BUTTON_INDICATOR_SELECTOR}`
  );
  ok(indicators.length, "Found indicator elements to check");

  for (const indicator of indicators) {
    ok(
      BrowserTestUtils.isHidden(indicator),
      "Indicator should never be visible in a normal (non-private) window"
    );
  }

  await BrowserTestUtils.closeWindow(win);
});

// The button + info panel below are gated on the design-refresh pref, so the
// tests set it explicitly rather than relying on the channel default.
const NOVA_PREF = "browser.nova.enabled";

function getVisibleIndicatorButton(win) {
  return [...win.document.querySelectorAll(BUTTON_INDICATOR_SELECTOR)].find(
    el => BrowserTestUtils.isVisible(el)
  );
}

// In a private window, the icon-only indicator button is shown and clicking it
// opens the info panel with a title, body copy, and learn-more link.
add_task(async function test_indicator_button_opens_panel() {
  await SpecialPowers.pushPrefEnv({ set: [[NOVA_PREF, true]] });
  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  let button = getVisibleIndicatorButton(win);
  Assert.ok(button, "Private browsing indicator button is visible");

  let panel = win.document.getElementById("private-browsing-info-panel");
  let shown = BrowserTestUtils.waitForEvent(panel, "popupshown");
  EventUtils.synthesizeMouseAtCenter(button, {}, win);
  await shown;

  Assert.ok(
    panel.querySelector(".private-browsing-info-title")?.textContent,
    "Panel has a title"
  );
  Assert.ok(
    panel.querySelector(".private-browsing-info-description")?.textContent,
    "Panel has body copy"
  );
  Assert.ok(
    panel.querySelector("a[is='moz-support-link']"),
    "Panel has a learn-more link"
  );

  let hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  panel.hidePopup();
  await hidden;

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

// Clicking the button again while the panel is open dismisses it, and the button
// reflects the open state via the [open] attribute while the panel is showing.
add_task(async function test_indicator_button_toggles_closed() {
  await SpecialPowers.pushPrefEnv({ set: [[NOVA_PREF, true]] });
  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  let button = getVisibleIndicatorButton(win);
  let panel = win.document.getElementById("private-browsing-info-panel");

  let shown = BrowserTestUtils.waitForEvent(panel, "popupshown");
  EventUtils.synthesizeMouseAtCenter(button, {}, win);
  await shown;
  Assert.ok(
    button.hasAttribute("open"),
    "Button has [open] while the panel is showing"
  );

  let hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  EventUtils.synthesizeMouseAtCenter(button, {}, win);
  await hidden;
  Assert.equal(
    panel.state,
    "closed",
    "Clicking the button again closes the panel"
  );
  Assert.ok(
    !button.hasAttribute("open"),
    "Button [open] is cleared after dismissal"
  );

  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

// Clicking the learn-more link inside the panel dismisses it.
add_task(async function test_indicator_link_closes_panel() {
  await SpecialPowers.pushPrefEnv({ set: [[NOVA_PREF, true]] });
  let win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  let button = getVisibleIndicatorButton(win);
  let panel = win.document.getElementById("private-browsing-info-panel");

  let shown = BrowserTestUtils.waitForEvent(panel, "popupshown");
  EventUtils.synthesizeMouseAtCenter(button, {}, win);
  await shown;

  // Stub link navigation so the click doesn't open a SUMO tab.
  let originalOpen = win.openTrustedLinkIn;
  win.openTrustedLinkIn = () => {};

  let link = panel.querySelector("a[is='moz-support-link']");
  let hidden = BrowserTestUtils.waitForEvent(panel, "popuphidden");
  EventUtils.synthesizeMouseAtCenter(link, {}, win);
  await hidden;
  Assert.equal(
    panel.state,
    "closed",
    "Panel closes when the learn-more link is clicked"
  );

  win.openTrustedLinkIn = originalOpen;
  await BrowserTestUtils.closeWindow(win);
  await SpecialPowers.popPrefEnv();
});

// The indicator button is not shown in a regular (non-private) window.
add_task(async function test_indicator_button_not_in_regular_window() {
  await SpecialPowers.pushPrefEnv({ set: [[NOVA_PREF, true]] });
  Assert.ok(
    !getVisibleIndicatorButton(window),
    "No indicator button in a regular window"
  );
  await SpecialPowers.popPrefEnv();
});
