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

const LOCATION_BADGE_DISMISSED_PREF =
  "browser.ipProtection.locationButtonBadgeDismissed";
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
 * Tests that clicking the location selection button in the status card opens
 * the locations subview.
 */
add_task(async function test_location_button_opens_subview() {
  let content = await openPanel({
    isProtectionEnabled: true,
  });

  let statusCard = content.statusCardEl;
  Assert.ok(statusCard, "ipprotection-status-card should be present");

  let locationButton = statusCard.locationButtonEl;
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

/**
 * Tests that the "new" badge on the location button is shown by default
 * when the dismissed pref is not set.
 */
add_task(async function test_location_button_badge_shown_by_default() {
  Services.prefs.clearUserPref(LOCATION_BADGE_DISMISSED_PREF);

  let content = await openPanel({
    isProtectionEnabled: true,
    showLocationButtonBadge: true,
  });

  let statusCard = content.statusCardEl;
  Assert.ok(statusCard, "ipprotection-status-card should be present");

  let locationButton = statusCard.locationButtonEl;
  let badge = locationButton.querySelector("moz-badge[type='new']");
  Assert.ok(badge, "Badge should be present when pref is not set");
  Assert.ok(BrowserTestUtils.isVisible(badge), "Badge should be visible");

  await closePanel();
  Services.prefs.clearUserPref(LOCATION_BADGE_DISMISSED_PREF);
});

/**
 * Tests that closing the panel permanently dismisses the badge by setting
 * the dismissed pref, and that the badge is hidden on subsequent opens.
 */
add_task(async function test_location_button_badge_dismissed_on_panel_close() {
  Services.prefs.clearUserPref(LOCATION_BADGE_DISMISSED_PREF);

  let content = await openPanel({
    isProtectionEnabled: true,
    showLocationButtonBadge: true,
  });

  let statusCard = content.statusCardEl;
  let locationButton = statusCard.locationButtonEl;
  let badge = locationButton.querySelector("moz-badge[type='new']");
  Assert.ok(badge, "Badge should be visible before panel is closed");

  await closePanel(window, false);

  Assert.ok(
    Services.prefs.getBoolPref(LOCATION_BADGE_DISMISSED_PREF, false),
    "locationButtonBadgeDismissed pref should be set after closing the panel"
  );

  content = await openPanel({ isProtectionEnabled: true });
  statusCard = content.statusCardEl;
  locationButton = statusCard.locationButtonEl;
  badge = locationButton.querySelector("moz-badge[type='new']");
  Assert.ok(!badge, "Badge should not be present after panel was closed");

  await closePanel();
  Services.prefs.clearUserPref(LOCATION_BADGE_DISMISSED_PREF);
});

/**
 * Tests that clicking the location selection button permanently dismisses
 * the badge by setting the dismissed pref.
 */
add_task(
  async function test_location_button_badge_dismissed_on_location_button_click() {
    Services.prefs.clearUserPref(LOCATION_BADGE_DISMISSED_PREF);

    let content = await openPanel({
      isProtectionEnabled: true,
      showLocationButtonBadge: true,
    });

    let statusCard = content.statusCardEl;
    let locationButton = statusCard.locationButtonEl;
    let badge = locationButton.querySelector("moz-badge[type='new']");
    Assert.ok(
      badge,
      "Badge should be visible before clicking the location button"
    );

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
      Services.prefs.getBoolPref(LOCATION_BADGE_DISMISSED_PREF, false),
      "locationButtonBadgeDismissed pref should be set after clicking the location button"
    );

    await closePanel(window, false);

    content = await openPanel({ isProtectionEnabled: true });
    statusCard = content.statusCardEl;
    locationButton = statusCard.locationButtonEl;
    badge = locationButton.querySelector("moz-badge[type='new']");
    Assert.ok(
      !badge,
      "Badge should not be present after location button was clicked"
    );

    await closePanel();
    Services.prefs.clearUserPref(LOCATION_BADGE_DISMISSED_PREF);
  }
);

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
