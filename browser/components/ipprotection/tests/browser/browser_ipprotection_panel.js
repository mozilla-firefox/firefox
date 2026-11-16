/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionWidget:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPProtectionPanel:
    "moz-src:///browser/components/ipprotection/IPProtectionPanel.sys.mjs",
  IPPProxyManager:
    "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs",
});

/**
 * Tests that clicking toolbar button opens the panel,
 * the panel contains a `<ipprotection-content>` element,
 * and the browser.ipProtection.everOpenedPanel pref is set
 */
add_task(async function click_toolbar_button() {
  let button = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);
  let panelView = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionWidget.PANEL_ID
  );

  let everOpenedPanel = Services.prefs.getBoolPref(
    "browser.ipProtection.everOpenedPanel",
    false
  );

  Assert.ok(!everOpenedPanel, "everOpenedPanel should be false");

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  // Open the panel
  button.click();
  await panelShownPromise;

  everOpenedPanel = Services.prefs.getBoolPref(
    "browser.ipProtection.everOpenedPanel",
    false
  );

  Assert.ok(everOpenedPanel, "everOpenedPanel should be true");

  let component = panelView.querySelector(
    lazy.IPProtectionPanel.CONTENT_TAGNAME
  );
  Assert.ok(
    BrowserTestUtils.isVisible(component),
    "ipprotection-content component should be present"
  );

  let header = panelView.querySelector(
    `#${lazy.IPProtectionPanel.HEADER_AREA_ID}`
  );
  Assert.ok(
    BrowserTestUtils.isVisible(header),
    "ipprotection-header component should be present"
  );

  // Close the panel
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHiddenPromise;

  Services.prefs.clearUserPref("browser.ipProtection.everOpenedPanel");
  Services.prefs.clearUserPref("browser.ipProtection.openedPanelSince160");
});

/**
 * Tests that opening the panel sets the
 * browser.ipProtection.openedPanelSince160 pref.
 */
add_task(async function test_opened_panel_since_160_pref() {
  let button = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);

  Assert.ok(
    !Services.prefs.getBoolPref("browser.ipProtection.openedPanelSince160"),
    "openedPanelSince160 should be false"
  );

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  button.click();
  await panelShownPromise;

  Assert.ok(
    Services.prefs.getBoolPref("browser.ipProtection.openedPanelSince160"),
    "openedPanelSince160 should be true"
  );

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHiddenPromise;

  Services.prefs.clearUserPref("browser.ipProtection.openedPanelSince160");
  Services.prefs.clearUserPref("browser.ipProtection.everOpenedPanel");
});

/**
 * Tests that the panel also loads the custom elements in a new window.
 */
add_task(async function test_panel_in_new_window() {
  let newWindow = await BrowserTestUtils.openNewBrowserWindow({
    url: "about:newtab",
  });
  newWindow.focus();

  let button = newWindow.document.getElementById(
    lazy.IPProtectionWidget.WIDGET_ID
  );
  let panelView = PanelMultiView.getViewNode(
    newWindow.document,
    lazy.IPProtectionWidget.PANEL_ID
  );

  let panelShownPromise = waitForPanelEvent(newWindow.document, "popupshown");
  // Open the panel
  button.click();
  await panelShownPromise;

  let component = panelView.querySelector(
    lazy.IPProtectionPanel.CONTENT_TAGNAME
  );
  Assert.ok(
    BrowserTestUtils.isVisible(component),
    "ipprotection-content component should be present"
  );

  let header = panelView.querySelector(
    `#${lazy.IPProtectionPanel.HEADER_AREA_ID}`
  );
  Assert.ok(
    BrowserTestUtils.isVisible(header),
    "ipprotection-header component should be present"
  );

  await BrowserTestUtils.closeWindow(newWindow);
});

/**
 * Tests that sending IPProtection:Close closes the panel.
 */
add_task(async function test_close_panel() {
  let button = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);
  let panelView = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionWidget.PANEL_ID
  );

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  // Open the panel
  button.click();
  await panelShownPromise;

  // Close the panel
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");

  panelView.dispatchEvent(
    new CustomEvent("IPProtection:Close", { bubbles: true })
  );

  await panelHiddenPromise;

  Assert.ok(!BrowserTestUtils.isVisible(panelView), "Panel should be closed");
});

/**
 * Tests that clicking the location selection button opens the locations
 * subview.
 */
add_task(async function test_location_button_opens_subview() {
  let content = await openPanel({
    isProtectionEnabled: true,
  });

  let locationButton = content.locationButtonEl;
  Assert.ok(locationButton, "Location selection button should be present");

  let subview = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionPanel.LOCATIONS_PANELVIEW
  );
  let locationsShownPromise = BrowserTestUtils.waitForEvent(
    subview,
    "ViewShown"
  );

  locationButton.click();

  await locationsShownPromise;

  Assert.ok(
    BrowserTestUtils.isVisible(subview),
    "Locations subview should be visible after clicking the location button"
  );

  Assert.ok(
    subview.querySelector(lazy.IPProtectionPanel.LOCATIONS_TAGNAME),
    "Locations component should be present"
  );

  await closePanel();
});

add_task(async function test_user_enable_count() {
  Services.prefs.clearUserPref("browser.ipProtection.userEnableCount");

  let initialCount = Services.prefs.getIntPref(
    "browser.ipProtection.userEnableCount",
    0
  );
  Assert.equal(initialCount, 0, "userEnableCount should start at 0");

  let startStub = sinon
    .stub(lazy.IPPProxyManager, "start")
    .resolves({ started: true });

  let button = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  button.click();
  await panelShownPromise;

  for (let i = 1; i <= 5; i++) {
    document.dispatchEvent(
      new CustomEvent("IPProtection:UserEnable", { bubbles: true })
    );

    let currentCount = Services.prefs.getIntPref(
      "browser.ipProtection.userEnableCount",
      0
    );

    let expectedCount = Math.min(i, 3);
    Assert.equal(
      currentCount,
      expectedCount,
      `userEnableCount should be ${expectedCount} after ${i} enable(s)`
    );
  }

  let finalCount = Services.prefs.getIntPref(
    "browser.ipProtection.userEnableCount",
    0
  );
  Assert.equal(
    finalCount,
    3,
    "userEnableCount should not exceed 3 even after 5 enables"
  );

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHiddenPromise;

  startStub.restore();
  Services.prefs.clearUserPref("browser.ipProtection.userEnableCount");
  Services.prefs.clearUserPref("browser.ipProtection.userEnabled");
});

/**
 * Tests showing the locations subview.
 */
add_task(async function test_show_locations() {
  let button = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);
  let panelView = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionPanel.MAIN_PANELVIEW
  );

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  // Open the panel
  button.click();
  await panelShownPromise;

  // Show the locations subview.
  let subview = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionPanel.LOCATIONS_PANELVIEW
  );
  let locationsShownPromise = BrowserTestUtils.waitForEvent(
    subview,
    "ViewShown"
  );

  // Switch to the locations view using the event directly.
  panelView.dispatchEvent(
    new CustomEvent("IPProtection:UserShowLocations", { bubbles: true })
  );

  await locationsShownPromise;

  Assert.ok(
    BrowserTestUtils.isVisible(subview),
    "Locations area should be visible"
  );

  Assert.ok(
    subview.querySelector(lazy.IPProtectionPanel.LOCATIONS_TAGNAME),
    "Locations component should be present"
  );

  Assert.ok(
    !subview.querySelector(".panel-info-button"),
    "Info button should not be present in locations view"
  );

  // Close the panel
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");

  panelView.dispatchEvent(
    new CustomEvent("IPProtection:Close", { bubbles: true })
  );

  await panelHiddenPromise;

  Assert.ok(!BrowserTestUtils.isVisible(panelView), "Panel should be closed");
});
