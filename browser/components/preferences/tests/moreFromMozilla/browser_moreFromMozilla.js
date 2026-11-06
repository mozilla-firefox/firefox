/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let mockFxA, unmockFxA;

add_setup(async function () {
  let { mock, unmock } = await mockDefaultFxAInstance();
  mockFxA = mock;
  unmockFxA = unmock;
});

add_task(async function test_config_renders_with_expected_cards() {
  await clearPolicies();
  let {
    grid,
    mobilePromo,
    monitorPromoCard,
    vpnPromoCard,
    relayPromoCard,
    mdnCard,
    thunderbirdCard,
    enterpriseCard,
    newProductsCard,
  } = await getPromoCards();

  ok(grid, "The products-grid container exists");

  ok(mobilePromo, "Firefox Mobile moz-promo is present");
  ok(
    mobilePromo.imageSrc.includes("more-from-mozilla-qr-code"),
    "QR code image src is set on the moz-promo"
  );

  ok(monitorPromoCard, "Mozilla Monitor card is present");
  is(
    monitorPromoCard.localName,
    "moz-card",
    "Monitor card is a moz-card element"
  );
  ok(
    monitorPromoCard.querySelector("moz-box-link"),
    "Monitor card contains a moz-box-link"
  );

  ok(vpnPromoCard, "VPN card is present");
  ok(relayPromoCard, "Relay card is present");
  ok(mdnCard, "MDN card is present");
  ok(thunderbirdCard, "Thunderbird card is present");
  ok(enterpriseCard, "Firefox Enterprise card is present");
  ok(newProductsCard, "Mozilla New Products card is present");

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_vpn_hidden_when_disabled() {
  await clearPolicies();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.vpn_promo.enabled", false]],
  });

  let { vpnPromoCard, monitorPromoCard } = await getPromoCards();
  ok(!vpnPromoCard, "The VPN promo card is not visible");
  ok(monitorPromoCard, "The Monitor card is visible");

  Services.prefs.clearUserPref("browser.vpn_promo.enabled");
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_vpn_hidden_in_disallowed_region() {
  await clearPolicies();
  setupRegions("SY");

  await SpecialPowers.pushPrefEnv({
    set: [["browser.vpn_promo.enabled", true]],
  });

  let { vpnPromoCard, monitorPromoCard } = await getPromoCards();
  ok(!vpnPromoCard, "VPN promo is hidden in disallowed region");
  ok(monitorPromoCard, "The Monitor card is visible");

  setupRegions(initialHomeRegion, initialCurrentRegion);
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

add_task(async function test_relay_hidden_with_custom_fxa() {
  await clearPolicies();
  unmockFxA();

  let { relayPromoCard } = await getPromoCards();
  ok(!relayPromoCard, "The Relay promo card is not visible");

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  mockFxA();
});

add_task(async function test_when_pref_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.preferences.moreFromMozilla", false]],
  });

  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;
  ok(
    !doc.querySelector(
      '#categories moz-page-nav-button[view="paneMoreFromMozilla"]'
    ),
    "More from Mozilla nav button is not present when pref is disabled"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_aboutpreferences_event_telemetry() {
  Services.fog.testResetFOG();

  await SpecialPowers.pushPrefEnv({
    set: [["browser.preferences.moreFromMozilla", true]],
  });
  await openPreferencesViaOpenPreferencesAPI("paneSync", {
    leaveOpen: true,
  });

  let doc = gBrowser.contentDocument;
  let navButton = doc.querySelector(
    '#categories moz-page-nav-button[view="paneMoreFromMozilla"]'
  );
  ok(navButton, "moreFromMozilla nav button is present");

  let paneShownPromise = BrowserTestUtils.waitForEvent(
    doc,
    "paneshown",
    false,
    e => e.detail.category === "paneMoreFromMozilla"
  );
  navButton.activate();
  await paneShownPromise;

  let showInitialEvents = Glean.aboutpreferences.showInitial.testGetValue();
  let showClickEvents = Glean.aboutpreferences.showClick.testGetValue();
  Assert.equal(showInitialEvents.length, 1, "One show initial event");
  Assert.equal(showClickEvents.length, 1, "One show click event");
  Assert.equal(
    showInitialEvents[0].extra.value,
    "paneSync",
    "Show initial on sync"
  );
  Assert.equal(
    showClickEvents[0].extra.value,
    "paneMoreFromMozilla",
    "Show click on More from Mozilla"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_aboutpreferences_search() {
  await clearPolicies();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.preferences.moreFromMozilla", true]],
  });

  await openPreferencesViaOpenPreferencesAPI(null, {
    leaveOpen: true,
  });

  await runSearchInput("More from Mozilla");

  let doc = gBrowser.contentDocument;
  let moreFromMozillaPane = doc.querySelector(
    'setting-pane[data-category="paneMoreFromMozilla"]'
  );
  ok(moreFromMozillaPane, "moreFromMozilla setting-pane is in the DOM");
  ok(
    BrowserTestUtils.isVisible(moreFromMozillaPane),
    "moreFromMozilla section is visible in search results for 'More from Mozilla'"
  );

  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_disabled_more_route_falls_back_to_sync() {
  await clearPolicies();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.preferences.moreFromMozilla", false]],
  });
  let { selectedPane } = await openPreferencesViaOpenPreferencesAPI(
    "paneMoreFromMozilla"
  );
  Assert.equal(
    selectedPane,
    "paneSync",
    "The disabled More from Mozilla pane falls back to Sync"
  );
  await SpecialPowers.popPrefEnv();
});
