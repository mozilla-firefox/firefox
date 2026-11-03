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

const MOCK_LOCKED_LOCATIONS_LIST = [
  { code: "US", available: true, locked: false },
  { code: "FR", available: true, locked: false },
  { code: "CA", available: true, locked: true },
  { code: "DE", available: true, locked: true },
];

const DEFAULT_CODES = MOCK_LOCKED_LOCATIONS_LIST.filter(l => !l.locked).map(
  l => l.code
);
const LOCKED_CODES = MOCK_LOCKED_LOCATIONS_LIST.filter(l => l.locked).map(
  l => l.code
);

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
    recButton.getAttribute("aria-selected"),
    "true",
    "recommended location should be selected by default"
  );

  let checkmark = recButton.querySelector(".location-check");
  Assert.ok(
    checkmark,
    "checkmark element should exist on the recommended button"
  );
  await TestUtils.waitForCondition(
    () => getComputedStyle(checkmark).visibility === "visible",
    "checkmark should be visible on selected item"
  );

  // Other items should not be selected
  for (let { code } of MOCK_LOCATIONS_LIST) {
    let unSelectedButton = locationsList.querySelector(
      `#location-option-${code}`
    );
    Assert.equal(
      unSelectedButton.getAttribute("aria-selected"),
      "false",
      `${code} button should not be selected`
    );
    await TestUtils.waitForCondition(
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
    caButton.getAttribute("aria-selected"),
    "true",
    "CA should be selected when passed as location"
  );

  let recButton = locationsList.querySelector("#location-option-REC");
  Assert.equal(
    recButton.getAttribute("aria-selected"),
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
    recButton.getAttribute("aria-selected"),
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
    "#locations-list li:not(:first-child)"
  );
  let renderedCodes = Array.from(locationItems).map(option =>
    option.id.replace("location-option-", "")
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

  await TestUtils.waitForCondition(
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

    await TestUtils.waitForCondition(
      () => switchStub.calledWith("CA"),
      "switch should be called with the selected country code"
    );

    switchStub.resetHistory();

    locationsList.querySelector("#location-option-REC").click();

    await TestUtils.waitForCondition(
      () => switchStub.calledWith(undefined),
      "switch should be called with undefined when REC is selected"
    );

    await closePanel();
    cleanupService();
    sandbox.restore();
  }
);

/**
 * Tests that an unavailable location cannot be selected via mouse click or
 * keyboard activation: switch is not called and neither the panel state nor
 * the egressLocation pref change.
 */
add_task(async function test_locations_list_unavailable_not_selectable() {
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

  let deButton = locationsList.querySelector("#location-option-DE");
  Assert.ok(deButton, "DE location button should be present");

  deButton.click();

  deButton.focus();
  EventUtils.synthesizeKey("KEY_Enter", {}, window);
  EventUtils.synthesizeKey(" ", {}, window);

  let panel = IPProtection.getPanel(window);

  Assert.ok(
    switchStub.notCalled,
    "switch should not be called for an unavailable location"
  );
  Assert.notEqual(
    panel.state.location,
    "DE",
    "panel state.location should not update to the unavailable code"
  );
  Assert.notEqual(
    Services.prefs.getStringPref(EGRESS_LOCATION_PREF, ""),
    "DE",
    "egressLocation pref should not hold the unavailable code"
  );

  await closePanel();
  cleanupService();
  sandbox.restore();
});

/**
 * Tests that disabled locations are rendered with the disabled attribute.
 */
add_task(async function test_locations_list_disabled_locations() {
  let { locationsList } = await openLocationsList({ location: null });

  let deButton = locationsList.querySelector("#location-option-DE");
  Assert.ok(deButton, "DE location button should be present");
  Assert.ok(
    deButton.hasAttribute("aria-disabled"),
    "unavailable location should be disabled"
  );
  Assert.ok(
    deButton.querySelector(".location-unavailable-label"),
    "unavailable location should have unavailable label"
  );

  let usButton = locationsList.querySelector("#location-option-US");
  Assert.ok(
    !usButton.hasAttribute("aria-disabled"),
    "available location should not be disabled"
  );
  Assert.ok(
    !usButton.querySelector(".location-unavailable-label"),
    "available location should not have unavailable label"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that a premium user sees every location, including locked ones,
 * plus the recommended location.
 */
add_task(async function test_locations_list_premium_shows_all_locations() {
  let { locationsList } = await openLocationsList();

  locationsList.locations = MOCK_LOCKED_LOCATIONS_LIST;
  locationsList.premium = true;
  await locationsList.updateComplete;

  for (let { code } of MOCK_LOCKED_LOCATIONS_LIST) {
    Assert.ok(
      locationsList.querySelector(`#location-option-${code}`),
      `${code} should be rendered for a premium user`
    );
  }

  let items = locationsList.querySelectorAll("#locations-list li");
  Assert.equal(
    items.length,
    MOCK_LOCKED_LOCATIONS_LIST.length + 1,
    "premium user should see every location plus the recommended location"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that a non-premium user only sees default locations; locked locations
 * are filtered out, while the recommended location is still shown.
 */
add_task(
  async function test_locations_list_non_premium_hides_locked_locations() {
    let { locationsList } = await openLocationsList();

    locationsList.locations = MOCK_LOCKED_LOCATIONS_LIST;
    locationsList.premium = false;
    await locationsList.updateComplete;

    for (let code of DEFAULT_CODES) {
      Assert.ok(
        locationsList.querySelector(`#location-option-${code}`),
        `${code} (default) should be rendered for a non-premium user`
      );
    }

    for (let code of LOCKED_CODES) {
      Assert.ok(
        !locationsList.querySelector(`#location-option-${code}`),
        `${code} (locked) should be hidden for a non-premium user`
      );
    }

    let items = locationsList.querySelectorAll("#locations-list li");
    Assert.equal(
      items.length,
      DEFAULT_CODES.length + 1,
      "non-premium user should see only default locations plus the recommended location"
    );

    await closePanel();
    cleanupService();
  }
);

/**
 * Tests that flipping the premium flag re-renders the list reactively:
 * locked locations appear once the user becomes premium.
 */
add_task(async function test_locations_list_premium_toggle_is_reactive() {
  let { locationsList } = await openLocationsList();

  locationsList.locations = MOCK_LOCKED_LOCATIONS_LIST;
  locationsList.premium = false;
  await locationsList.updateComplete;

  Assert.ok(
    !locationsList.querySelector(`#location-option-${LOCKED_CODES[0]}`),
    "lccked location should be hidden while non-premium"
  );

  locationsList.premium = true;
  await locationsList.updateComplete;

  Assert.ok(
    locationsList.querySelector(`#location-option-${LOCKED_CODES[0]}`),
    "locked location should appear after becoming premium"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that when a locked location is selected but the user is not premium,
 * the selection resolves to the recommended location, so the filtered
 * listbox still has exactly one selected option.
 */
add_task(
  async function test_locations_list_locked_selection_falls_back_to_rec() {
    let { locationsList } = await openLocationsList();

    locationsList.locations = MOCK_LOCKED_LOCATIONS_LIST;
    locationsList.premium = false;
    locationsList.selectedLocation = LOCKED_CODES[0];
    await locationsList.updateComplete;

    Assert.equal(
      locationsList.getSelectedLocation(),
      "REC",
      "a selected locked location should resolve to REC for a non-premium user"
    );

    Assert.ok(
      !locationsList.querySelector(`#location-option-${LOCKED_CODES[0]}`),
      "the selected locked location should not be rendered"
    );

    let recButton = locationsList.querySelector("#location-option-REC");
    Assert.equal(
      recButton.getAttribute("aria-selected"),
      "true",
      "recommended location should be the selected option"
    );

    let selectedOptions = locationsList.querySelectorAll(
      '#locations-list [aria-selected="true"]'
    );
    Assert.equal(
      selectedOptions.length,
      1,
      "exactly one option should be selected after falling back to REC"
    );

    await closePanel();
    cleanupService();
  }
);

/*
 * Tests that the user account is marked as premium when hasUpgraded is true or isDefaultBrowser is true
 */
add_task(async function test_user_set_to_premium() {
  let sandbox = sinon.createSandbox();
  const { ShellService } = ChromeUtils.importESModule(
    "moz-src:///browser/components/shell/ShellService.sys.mjs"
  );
  let isDefaultBrowserStub = sandbox
    .stub(ShellService, "isDefaultBrowserAsync")
    .resolves(false);

  setupService({ isReady: true, hasUpgraded: true });
  let { locationsList } = await openLocationsList({ hasUpgraded: true });

  Assert.ok(
    locationsList.premium,
    "user should be premium when hasUpgraded is true"
  );

  await closePanel();
  cleanupService();

  isDefaultBrowserStub.resolves(true);
  setupService({ isReady: true, hasUpgraded: false });
  ({ locationsList } = await openLocationsList({ hasUpgraded: false }));

  Assert.ok(
    locationsList.premium,
    "user should be premium when isDefaultBrowser is true"
  );

  await closePanel();
  cleanupService();
  sandbox.restore();
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

/**
 * Tests that the locations subview waits for the serverlist before showing,
 * so it is not shown empty and then resized once the list arrives.
 */
add_task(async function test_locations_subview_waits_for_serverlist() {
  const { IPProtectionServerlist } = ChromeUtils.importESModule(
    "moz-src:///toolkit/components/ipprotection/IPProtectionServerlist.sys.mjs"
  );
  let fetched = Promise.withResolvers();
  let realGet = IPProtectionServerlist.bucket.get.bind(
    IPProtectionServerlist.bucket
  );
  let getStub = sinon
    .stub(IPProtectionServerlist.bucket, "get")
    .callsFake(async () => {
      await fetched.promise;
      return realGet();
    });
  IPProtectionServerlist.__list = [];
  await openPanel({ isReady: true, locationsList: [] });

  let panel = IPProtection.getPanel(window);
  let locationsView = PanelMultiView.getViewNode(
    document,
    IPProtectionPanel.LOCATIONS_PANELVIEW
  );
  let locationsOnShown = BrowserTestUtils.waitForEvent(
    locationsView,
    "ViewShown"
  ).then(() => panel.state.locationsList);

  panel.showLocationSelector();
  Assert.ok(
    !locationsView.querySelector(IPProtectionPanel.LOCATIONS_TAGNAME),
    "the subview should not be shown while the serverlist is loading"
  );

  fetched.resolve();
  Assert.deepEqual(
    (await locationsOnShown).map(l => l.code),
    ["US"],
    "the serverlist should be loaded when the subview is shown"
  );

  getStub.restore();
  await closePanel();
  cleanupService();
});
