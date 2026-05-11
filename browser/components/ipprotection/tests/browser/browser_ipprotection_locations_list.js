/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { LINKS } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

const MOCK_LOCATIONS_LIST = [
  { code: "US", available: true },
  { code: "CA", available: true },
  { code: "DE", available: false },
];

/**
 * Opens the IP Protection panel, then navigates to the locations subview.
 * Returns the locations-list element.
 *
 * @param {object} state - Additional state to set on the panel.
 * @returns {Promise<{locationsList: Element, locationsView: Element}>}
 */
async function openLocationsList(state = {}) {
  await openPanel({
    isReady: true,
    locationsList: MOCK_LOCATIONS_LIST,
    ...state,
  });

  let panel = IPProtection.getPanel(window);
  let locationsView = PanelMultiView.getViewNode(
    document,
    IPProtectionPanel.LOCATIONS_PANELVIEW
  );

  let viewShownPromise = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewShown"
  );
  panel.showLocationSelector();
  await viewShownPromise;

  let locationsEl = locationsView.querySelector(
    IPProtectionPanel.LOCATIONS_TAGNAME
  );
  await locationsEl?.updateComplete;

  let locationsList = locationsView.querySelector("locations-list");
  await locationsList?.updateComplete;

  return { locationsList, locationsView, locationsEl };
}

/**
 * Tests that the locations list renders correctly with a locations list
 * and that the recommended location is selected by default.
 */
add_task(async function test_locations_list_default_rendering() {
  let { locationsList } = await openLocationsList({ location: null });

  Assert.ok(locationsList, "locations-list element should exist");

  let description = locationsList.querySelector("#locations-list-description");
  Assert.ok(description, "locations list description should be present");

  let locationList = locationsList.querySelector("#locations-list");
  Assert.ok(locationList, "location list should be present");

  let locationItems = locationList.querySelectorAll("li");
  Assert.equal(
    locationItems.length,
    MOCK_LOCATIONS_LIST.length + 1,
    "location list should contain all countries plus the recommended location"
  );

  let recButton = locationsList.querySelector("#location-option-REC");
  Assert.ok(recButton, "recommended location button should be present");
  Assert.equal(
    recButton.getAttribute("aria-checked"),
    "true",
    "recommended location should be selected by default"
  );

  let checkmark = recButton.querySelector(".location-check");
  Assert.ok(
    checkmark,
    "checkmark element should exist on the recommended button"
  );
  await BrowserTestUtils.waitForCondition(
    () => getComputedStyle(checkmark).visibility === "visible",
    "checkmark should be visible on selected item"
  );

  // Other items should not be selected
  for (let { code } of MOCK_LOCATIONS_LIST) {
    let unSelectedButton = locationsList.querySelector(
      `#location-option-${code}`
    );
    Assert.equal(
      unSelectedButton.getAttribute("aria-checked"),
      "false",
      `${code} button should not be selected`
    );
    await BrowserTestUtils.waitForCondition(
      () =>
        getComputedStyle(unSelectedButton.querySelector(".location-check"))
          .visibility === "hidden",
      `checkmark should be hidden on unselected ${code} button`
    );
  }

  await closePanel();
  cleanupService();
});

/**
 * Tests that a pre-selected location is shown as selected.
 */
add_task(async function test_locations_list_preselected_location() {
  let { locationsList } = await openLocationsList({ location: "CA" });

  Assert.ok(locationsList, "locations-list element should exist");

  let caButton = locationsList.querySelector("#location-option-CA");
  Assert.ok(caButton, "CA location button should be present");
  Assert.equal(
    caButton.getAttribute("aria-checked"),
    "true",
    "CA should be selected when passed as location"
  );

  let recButton = locationsList.querySelector("#location-option-REC");
  Assert.equal(
    recButton.getAttribute("aria-checked"),
    "false",
    "recommended location should not be selected"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that an unknown selectedLocation falls back to the recommended location.
 */
add_task(async function test_locations_list_unknown_falls_back_to_rec() {
  let { locationsList } = await openLocationsList({ location: "invalidCode" });

  Assert.ok(locationsList, "locations-list element should exist");

  Assert.equal(
    locationsList.getSelectedLocation(),
    "REC",
    "getSelectedLocation should fall back to REC when an invalid code is passed"
  );

  let recButton = locationsList.querySelector("#location-option-REC");
  Assert.equal(
    recButton.getAttribute("aria-checked"),
    "true",
    "recommended location button should be selected when an invalid code is passed"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that locations except for the recommended location are
 * rendered in alphabetical order by their localized country name.
 */
add_task(async function test_locations_list_sorted_alphabetically() {
  let { locationsList } = await openLocationsList({
    locationsList: [
      { code: "DE", available: true },
      { code: "US", available: true },
      { code: "CA", available: true },
    ],
  });

  Assert.ok(locationsList, "locations-list element should exist");

  let locationItems = locationsList.querySelectorAll(
    "#locations-list li:not(:first-child) button"
  );
  let renderedCodes = Array.from(locationItems).map(btn =>
    btn.id.replace("location-option-", "")
  );

  let expectedCodes = ["CA", "DE", "US"];

  Assert.deepEqual(
    renderedCodes,
    expectedCodes,
    "locations should be rendered in alphabetical order by localized country name"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that selecting a location writes the code to the egressLocation pref
 * and propagates to the panel state.
 */
add_task(async function test_locations_list_selection_persists_to_pref() {
  const EGRESS_LOCATION_PREF = "browser.ipProtection.egressLocation";

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref(EGRESS_LOCATION_PREF);
  });

  let { locationsList } = await openLocationsList({ location: null });

  let caButton = locationsList.querySelector("#location-option-CA");
  Assert.ok(caButton, "CA location button should be present");

  caButton.click();

  let panel = IPProtection.getPanel(window);

  await BrowserTestUtils.waitForCondition(
    () => panel.state.location === "CA",
    "panel state.location should update to the selected code"
  );

  Assert.equal(
    Services.prefs.getStringPref(EGRESS_LOCATION_PREF, ""),
    "CA",
    "egressLocation pref should hold the selected code"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that selecting a country while the proxy is active calls
 * IPPProxyManager.switch with the selected code, and that "REC"
 * switches to recommended location.
 */
add_task(
  async function test_locations_list_selection_calls_switch_when_active() {
    const EGRESS_LOCATION_PREF = "browser.ipProtection.egressLocation";

    registerCleanupFunction(() => {
      Services.prefs.clearUserPref(EGRESS_LOCATION_PREF);
    });

    let sandbox = sinon.createSandbox();
    sandbox.stub(IPPProxyManager, "state").get(() => IPPProxyStates.ACTIVE);

    let switchStub = sandbox.stub(IPPProxyManager, "switch").returns({
      switched: true,
    });

    let { locationsList } = await openLocationsList({ location: null });

    locationsList.querySelector("#location-option-CA").click();

    await BrowserTestUtils.waitForCondition(
      () => switchStub.calledWith("CA"),
      "switch should be called with the selected country code"
    );

    switchStub.resetHistory();

    locationsList.querySelector("#location-option-REC").click();

    await BrowserTestUtils.waitForCondition(
      () => switchStub.calledWith(undefined),
      "switch should be called with undefined when REC is selected"
    );

    await closePanel();
    cleanupService();
    sandbox.restore();
  }
);

/**
 * Tests that disabled locations are rendered with the disabled attribute.
 */
add_task(async function test_locations_list_disabled_locations() {
  let { locationsList } = await openLocationsList({ location: null });

  let deButton = locationsList.querySelector("#location-option-DE");
  Assert.ok(deButton, "DE location button should be present");
  Assert.ok(deButton.disabled, "unavailable location should be disabled");

  let usButton = locationsList.querySelector("#location-option-US");
  Assert.ok(!usButton.disabled, "available location should not be disabled");

  await closePanel();
  cleanupService();
});

/**
 * Tests that moz-promo is shown when not upgraded
 */
add_task(async function test_promo_shown_when_not_upgraded() {
  let { locationsEl } = await openLocationsList({ hasUpgraded: false });

  let promo = locationsEl.querySelector("moz-promo#locations-subview-promo");
  Assert.ok(promo, "moz-promo should be present when user has not upgraded");
  Assert.equal(
    promo.getAttribute("imagealignment"),
    "end",
    "promo should have imagealignment='end'"
  );
  Assert.ok(
    promo.getAttribute("imagesrc"),
    "promo should have an imagesrc attribute"
  );

  let button = promo.querySelector("moz-button");
  Assert.ok(button, "promo should have an actions button");
  let openWebLinkInStub = sinon.stub(window, "openWebLinkIn");
  button.click();
  Assert.ok(
    openWebLinkInStub.calledOnce,
    "openWebLinkIn should be called once"
  );
  cleanupService();
});

/**
 * Tests that moz-promo is not shown when the user has upgraded.
 */
add_task(async function test_promo_not_shown_when_upgraded() {
  let { locationsEl } = await openLocationsList({ hasUpgraded: true });

  let promo = locationsEl.querySelector("moz-promo");
  Assert.ok(!promo, "moz-promo should not be present when user has upgraded");

  await closePanel();
  cleanupService();
});
