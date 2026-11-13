/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { LINKS } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

const MOCK_LOCATIONS_LIST = [
  { code: "CA", available: true },
  { code: "US", available: true },
  { code: "DE", available: false },
];

/**
 * Tests that clicking the location selector button records
 * location_selector_button_clicked, and that clicking the promo upgrade
 * button records location_upgrade_promo_clicked.
 */
add_task(async function test_location_selector_button_and_promo_clicked() {
  let content = await openPanel({
    isEnrolledAndEntitled: true,
    hasUpgraded: false,
    locationsList: MOCK_LOCATIONS_LIST,
    location: null,
  });

  let statusCard = content.shadowRoot.querySelector("ipprotection-status-card");
  Assert.ok(statusCard, "Status card should be present");

  let locationButton = statusCard.locationButtonEl;
  Assert.ok(locationButton, "Location selector button should be present");

  let locationsView = PanelMultiView.getViewNode(
    document,
    IPProtectionPanel.LOCATIONS_PANELVIEW
  );

  Services.fog.testResetFOG();
  await Services.fog.testFlushAllChildren();

  // Click the location selector button
  let viewShownPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewShown"
  );
  locationButton.click();
  await viewShownPromise;

  let selectorButtonEvents =
    Glean.ipprotection.locationSelectorButtonClicked.testGetValue();
  Assert.equal(
    selectorButtonEvents.length,
    1,
    "should have recorded one location_selector_button_clicked event"
  );
  Assert.equal(selectorButtonEvents[0].category, "ipprotection");
  Assert.equal(
    selectorButtonEvents[0].name,
    "location_selector_button_clicked"
  );

  let locationsEl = locationsView.querySelector(
    IPProtectionPanel.LOCATIONS_TAGNAME
  );
  await locationsEl?.updateComplete;

  // Click the promo button
  let promo = locationsEl.querySelector("moz-promo#locations-subview-promo");
  Assert.ok(promo, "promo should be present for non-upgraded users");

  let promoButton = promo.querySelector("moz-button");
  Assert.ok(promoButton, "promo button should be present");

  let openWebLinkInStub = sinon.stub(window, "openWebLinkIn");
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  promoButton.click();
  await panelHiddenPromise;
  openWebLinkInStub.restore();

  Assert.ok(
    openWebLinkInStub.calledOnceWith(LINKS.LOCATION_PROMO_URL, "tab"),
    "openWebLinkIn should be called with the location promo URL"
  );

  let promoClickedEvents =
    Glean.ipprotection.locationUpgradePromoClicked.testGetValue();
  Assert.equal(
    promoClickedEvents.length,
    1,
    "should have recorded one location_upgrade_promo_clicked event"
  );
  Assert.equal(promoClickedEvents[0].category, "ipprotection");
  Assert.equal(promoClickedEvents[0].name, "location_upgrade_promo_clicked");

  Services.fog.testResetFOG();
  cleanupService();
});

/**
 * Tests that selecting a location from the location selector records
 * location_changed with the correct country code.
 */
add_task(async function test_location_changed() {
  await openPanel({
    isEnrolledAndEntitled: true,
    locationsList: MOCK_LOCATIONS_LIST,
    location: null,
  });

  let locationsView = PanelMultiView.getViewNode(
    document,
    IPProtectionPanel.LOCATIONS_PANELVIEW
  );

  let viewShownPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewShown"
  );
  IPProtection.getPanel(window).showLocationSelector();
  await viewShownPromise;

  let locationsList = locationsView.querySelector("locations-list");
  await locationsList?.updateComplete;

  Services.fog.testResetFOG();
  await Services.fog.testFlushAllChildren();

  // Select the first non-REC location
  let firstLocation = locationsList.querySelectorAll(".location-item")[1];
  Assert.ok(firstLocation, "First location button should be present");
  const expectedCode = firstLocation.id.replace("location-option-", "");

  let viewHiddenPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewHiding"
  );
  firstLocation.click();
  await viewHiddenPromise;

  let locationChangedEvents = Glean.ipprotection.locationChanged.testGetValue();
  Assert.equal(
    locationChangedEvents.length,
    1,
    "should have recorded one location_changed event"
  );
  Assert.equal(locationChangedEvents[0].category, "ipprotection");
  Assert.equal(locationChangedEvents[0].name, "location_changed");
  Assert.equal(
    locationChangedEvents[0].extra.location,
    expectedCode,
    "location_changed should record the selected country code"
  );

  Services.fog.testResetFOG();
  await closePanel();
  cleanupService();
  Services.prefs.clearUserPref("browser.ipProtection.egressLocation");
});
