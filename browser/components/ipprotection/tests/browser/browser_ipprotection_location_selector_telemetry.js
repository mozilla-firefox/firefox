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
 * button opens the location promo URL.
 */
add_task(async function test_location_selector_button_and_promo_clicked() {
  let isDefaultStub = sinon
    .stub(IPProtection.getPanel(window), "isDefaultBrowser")
    .resolves(true);
  let content = await openPanel({
    isEnrolledAndEntitled: true,
    hasUpgraded: false,
    locationsList: MOCK_LOCATIONS_LIST,
    location: null,
  });

  let locationButton = content.locationButtonEl;
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

  isDefaultStub.restore();
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

async function showLocationsSubview() {
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

  let locationsEl = locationsView.querySelector(
    IPProtectionPanel.LOCATIONS_TAGNAME
  );
  await locationsEl?.updateComplete;
  return locationsEl;
}

async function goBackToMainView() {
  let mainView = PanelMultiView.getViewNode(
    document,
    IPProtectionPanel.MAIN_PANELVIEW
  );
  let viewShownPromise = BrowserTestUtils.waitForEvent(mainView, "ViewShown");
  IPProtection.getPanel(window).panelMultiView.goBack();
  await viewShownPromise;
}

function getPromoExtras(metric) {
  return (metric.testGetValue() ?? []).map(e => e.extra);
}

/**
 * Tests that promo_seen is recorded once each time the locations subview is
 * shown with the Mozilla VPN promo, not on every re-render, and that clicking
 * the promo records promo_clicked.
 */
add_task(async function test_upgrade_promo_seen_and_clicked() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(IPProtection.getPanel(window), "isDefaultBrowser")
    .resolves(true);
  await openPanel({
    isEnrolledAndEntitled: true,
    hasUpgraded: false,
    locationsList: MOCK_LOCATIONS_LIST,
    location: null,
  });

  Services.fog.testResetFOG();
  await Services.fog.testFlushAllChildren();

  let locationsEl = await showLocationsSubview();
  Assert.ok(
    locationsEl.querySelector("moz-promo#locations-subview-promo"),
    "Mozilla VPN promo should be present"
  );
  Assert.deepEqual(
    getPromoExtras(Glean.ipprotection.promoSeen),
    [{ promo: "mozillavpn", entrypoint: "panel" }],
    "should record promo_seen when the subview is shown"
  );

  IPProtection.getPanel(window).setState({});
  await locationsEl.updateComplete;
  Assert.deepEqual(
    getPromoExtras(Glean.ipprotection.promoSeen),
    [{ promo: "mozillavpn", entrypoint: "panel" }],
    "should not record promo_seen again on re-render"
  );

  await goBackToMainView();
  locationsEl = await showLocationsSubview();
  Assert.deepEqual(
    getPromoExtras(Glean.ipprotection.promoSeen),
    [
      { promo: "mozillavpn", entrypoint: "panel" },
      { promo: "mozillavpn", entrypoint: "panel" },
    ],
    "should record promo_seen again when the subview is reopened"
  );

  sandbox.stub(window, "openWebLinkIn");
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  locationsEl
    .querySelector("moz-promo#locations-subview-promo moz-button")
    .click();
  await panelHiddenPromise;

  Assert.deepEqual(
    getPromoExtras(Glean.ipprotection.promoClicked),
    [{ promo: "mozillavpn", entrypoint: "panel" }],
    "should record promo_clicked for the Mozilla VPN promo"
  );

  sandbox.restore();
  Services.fog.testResetFOG();
  cleanupService();
});

/**
 * Tests that promo_seen and promo_clicked are recorded for the set default
 * promo.
 */
add_task(async function test_set_default_promo_seen_and_clicked() {
  let sandbox = sinon.createSandbox();
  let isDefaultStub = sandbox
    .stub(IPProtection.getPanel(window), "isDefaultBrowser")
    .resolves(false);
  sandbox.stub(SpecialMessageActions, "setDefaultBrowser").resolves();
  await openPanel({
    isEnrolledAndEntitled: true,
    hasUpgraded: false,
    locationsList: MOCK_LOCATIONS_LIST,
    location: null,
  });

  Services.fog.testResetFOG();
  await Services.fog.testFlushAllChildren();

  let locationsEl = await showLocationsSubview();
  let button = locationsEl.querySelector(
    "moz-promo#locations-subview-set-default-promo moz-button"
  );
  Assert.ok(button, "set default promo button should be present");
  Assert.deepEqual(
    getPromoExtras(Glean.ipprotection.promoSeen),
    [{ promo: "defaultbrowser", entrypoint: "panel" }],
    "should record promo_seen for the set default promo"
  );

  isDefaultStub.resolves(true);
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  button.click();
  await panelHiddenPromise;

  Assert.deepEqual(
    getPromoExtras(Glean.ipprotection.promoClicked),
    [{ promo: "defaultbrowser", entrypoint: "panel" }],
    "should record promo_clicked for the set default promo"
  );

  sandbox.restore();
  Services.fog.testResetFOG();
  cleanupService();
});

/**
 * Tests that promo_seen is recorded once per promo while the subview is
 * shown, even if the visible promo switches back and forth.
 */
add_task(async function test_promo_seen_once_per_promo_when_switching() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(IPProtection.getPanel(window), "isDefaultBrowser")
    .resolves(false);
  await openPanel({
    isEnrolledAndEntitled: true,
    hasUpgraded: false,
    locationsList: MOCK_LOCATIONS_LIST,
    location: null,
  });

  Services.fog.testResetFOG();
  await Services.fog.testFlushAllChildren();

  let locationsEl = await showLocationsSubview();
  Assert.ok(
    locationsEl.querySelector("moz-promo#locations-subview-set-default-promo"),
    "set default promo should be present"
  );

  let panel = IPProtection.getPanel(window);
  panel.setState({ isDefaultBrowser: true });
  await locationsEl.updateComplete;
  Assert.ok(
    locationsEl.querySelector("moz-promo#locations-subview-promo"),
    "Mozilla VPN promo should be present"
  );

  panel.setState({ isDefaultBrowser: false });
  await locationsEl.updateComplete;
  Assert.ok(
    locationsEl.querySelector("moz-promo#locations-subview-set-default-promo"),
    "set default promo should be present again"
  );

  Assert.deepEqual(
    getPromoExtras(Glean.ipprotection.promoSeen),
    [
      { promo: "defaultbrowser", entrypoint: "panel" },
      { promo: "mozillavpn", entrypoint: "panel" },
    ],
    "should record promo_seen once per promo while the subview is shown"
  );

  await closePanel();
  sandbox.restore();
  Services.fog.testResetFOG();
  cleanupService();
});

/**
 * Tests that promo_seen is not recorded when no promo is shown.
 */
add_task(async function test_no_promo_seen_when_upgraded() {
  await openPanel({
    isEnrolledAndEntitled: true,
    hasUpgraded: true,
    locationsList: MOCK_LOCATIONS_LIST,
    location: null,
  });

  Services.fog.testResetFOG();
  await Services.fog.testFlushAllChildren();

  let locationsEl = await showLocationsSubview();
  Assert.ok(
    !locationsEl.querySelector("moz-promo"),
    "no promo should be shown for upgraded users"
  );
  Assert.equal(
    Glean.ipprotection.promoSeen.testGetValue(),
    null,
    "should not record promo_seen when no promo is shown"
  );

  Services.fog.testResetFOG();
  await closePanel();
  cleanupService();
});
