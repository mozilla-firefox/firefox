/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { LINKS, BANDWIDTH } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);
const { countryName } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-utils.mjs"
);

const mockLocation = "US";

const mockBandwidthUsage = {
  remaining: 30 * BANDWIDTH.BYTES_IN_GB,
  remainingMB: 30 * (BANDWIDTH.BYTES_IN_GB / BANDWIDTH.BYTES_IN_MB),
  remainingGB: 30,
  max: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
  maxGB: BANDWIDTH.MAX_IN_GB,
  used: 20 * BANDWIDTH.BYTES_IN_GB,
  usedGB: 20,
  percent: "40",
  remainingRounded: 30,
  gbCount: 2,
  mbCount: 0,
};

async function setupStatusCardTest(opts = { bandwidthEnabled: true }) {
  const { bandwidthEnabled } = opts;
  setupService({
    isReady: true,
    canEnroll: true,
    proxyPass: {
      status: 200,
      error: undefined,
      pass: makePass(),
      usage: makeUsage(),
    },
    usageInfo: null,
  });
  IPProtectionService.updateState();
  await waitForProxyState(IPPProxyStates.READY);

  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.bandwidth.enabled", bandwidthEnabled]],
  });
}

async function cleanupStatusCardTest() {
  await SpecialPowers.popPrefEnv();
  cleanupService();
}

/**
 * Tests the disconnected state UI.
 */
add_task(async function test_status_card_disconnected() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: false,
    bandwidthUsage: mockBandwidthUsage,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );

  let statusCard = content.statusCardEl;
  Assert.ok(content.statusCardEl, "ipprotection-status-card should be present");

  let statusBoxEl = statusCard.statusBoxEl;
  Assert.ok(statusBoxEl, "Status box should be present");
  await checkStatusBoxAriaLabel(statusBoxEl);

  checkBandwidth(content.bandwidthUsageEl, mockBandwidthUsage);

  const turnOnButtonEl = statusCard.actionButtonEl;
  Assert.ok(turnOnButtonEl, "Button to turn on VPN should be present");

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Tests the connected state UI.
 */
add_task(async function test_status_card_connected() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );

  let statusCard = content.statusCardEl;
  Assert.ok(content.statusCardEl, "ipprotection-status-card should be present");

  let statusBoxEl = statusCard.statusBoxEl;
  Assert.ok(statusBoxEl, "Status box should be present");
  await checkStatusBoxAriaLabel(statusBoxEl);

  checkBandwidth(content.bandwidthUsageEl, mockBandwidthUsage);

  const turnOffVPNButtonEl = statusCard.actionButtonEl;
  Assert.ok(turnOffVPNButtonEl, "Button to turn off VPN should be present");

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Tests that the correct IPProtection events are dispatched when
 * we enable or disable VPN protection.
 */
add_task(async function test_ipprotection_events_on_toggle() {
  // These events are different from the ones sent by
  // ipprotection-status-card. The prefixed "IPProtection:" events
  // actually change the connection state in the service when dispatched.
  // If the IPProtection events are sent, then we know that the status-card
  // events worked.
  const userEnableEventName = "IPProtection:UserEnable";
  const userDisableEventName = "IPProtection:UserDisable";

  // Reset service state.
  cleanupService();
  IPProtectionService.updateState();

  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: false,
    bandwidthUsage: mockBandwidthUsage,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );

  let statusCard = content.statusCardEl;
  Assert.ok(content.statusCardEl, "ipprotection-status-card should be present");

  let statusBoxEl = statusCard.statusBoxEl;
  Assert.ok(statusBoxEl, "Status box should be present");

  // Now click "Turn on"
  let turnOnVPNButtonEl = statusCard.actionButtonEl;
  Assert.ok(turnOnVPNButtonEl, "Button to turn on VPN should be present");

  let startedProxyPromise = BrowserTestUtils.waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    false,
    () => !!IPPProxyManager.activatedAt
  );
  let enableEventPromise = BrowserTestUtils.waitForEvent(
    window,
    userEnableEventName
  );

  turnOnVPNButtonEl.click();

  await Promise.all([startedProxyPromise, enableEventPromise]);

  Assert.ok(
    true,
    "Enable event and proxy started event were found after clicking the toggle"
  );

  let userEnabledPref = Services.prefs.getBoolPref(
    "browser.ipProtection.userEnabled",
    false
  );
  Assert.equal(userEnabledPref, true, "userEnabled pref should be set to true");

  // Now click "Turn off"
  let turnOffVPNButtonEl = statusCard.actionButtonEl;
  Assert.ok(turnOffVPNButtonEl, "Button to turn off VPN should be present");

  let stoppedProxyPromise = BrowserTestUtils.waitForEvent(
    IPPProxyManager,
    "IPPProxyManager:StateChanged",
    false,
    () => !IPPProxyManager.activatedAt
  );
  let disableEventPromise = BrowserTestUtils.waitForEvent(
    window,
    userDisableEventName
  );

  turnOffVPNButtonEl.click();

  await Promise.all([stoppedProxyPromise, disableEventPromise]);
  Assert.ok(
    true,
    "Disable event and stopped proxy event were found after clicking the toggle"
  );

  userEnabledPref = Services.prefs.getBoolPref(
    "browser.ipProtection.userEnabled",
    true
  );
  Assert.equal(
    userEnabledPref,
    false,
    "userEnabled pref should be set to false"
  );

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Tests the excluded site state UI.
 */
add_task(async function test_status_card_excluded() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.features.siteInclusions", false]],
  });
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(IPPSiteRuleManager, "getRule")
    .returns(IPPPrincipalRules.EXCLUDED);

  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );

  let statusCard = content.statusCardEl;
  Assert.ok(content.statusCardEl, "ipprotection-status-card should be present");

  let statusBoxEl = statusCard.statusBoxEl;
  Assert.ok(statusBoxEl, "Status box should be present");
  await checkStatusBoxAriaLabel(statusBoxEl);

  Assert.equal(
    statusBoxEl.type,
    "excluded",
    "Status box should have excluded type"
  );

  checkBandwidth(content.bandwidthUsageEl, mockBandwidthUsage);

  const turnOffVPNButtonEl = statusCard.actionButtonEl;
  Assert.ok(turnOffVPNButtonEl, "Button to turn off VPN should be present");

  await closePanel();
  await cleanupStatusCardTest();
  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that an excluded site shows the connected state when site inclusions
 * are enabled.
 */
add_task(async function test_status_card_excluded_site_inclusions_enabled() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(IPPSiteRuleManager, "getRule")
    .returns(IPPPrincipalRules.EXCLUDED);

  await setupStatusCardTest();
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.features.siteInclusions", true]],
  });

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
  });

  let statusBoxEl = content.statusCardEl.statusBoxEl;
  Assert.ok(statusBoxEl, "Status box should be present");
  await checkStatusBoxAriaLabel(statusBoxEl);

  Assert.equal(
    statusBoxEl.type,
    "connected",
    "Status box should have connected type"
  );
  Assert.equal(
    statusBoxEl.headerL10nId,
    "ipprotection-connection-status-connected-1",
    "Status box should show the connected header"
  );
  Assert.equal(
    content.statusCardEl.actionButtonEl.dataset.l10nId,
    "ipprotection-button-turn-vpn-off-1",
    "Button should use the regular turn off label"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
  await cleanupStatusCardTest();
  sandbox.restore();
});

/**
 * Tests the connecting state UI.
 */
add_task(async function test_status_card_connecting() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
    isActivating: true,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );

  let statusCard = content.statusCardEl;
  Assert.ok(content.statusCardEl, "ipprotection-status-card should be present");

  let statusBoxEl = statusCard.statusBoxEl;
  Assert.ok(statusBoxEl, "Status box should be present");
  await checkStatusBoxAriaLabel(statusBoxEl);

  Assert.equal(
    statusBoxEl.type,
    "connecting",
    "Status box should have connecting type"
  );

  checkBandwidth(content.bandwidthUsageEl, mockBandwidthUsage);

  const button = statusCard.actionButtonEl;
  Assert.ok(
    button?.disabled,
    "Button in connecting state should be present and disabled"
  );

  const locationButton = content.locationButtonEl;
  Assert.ok(
    locationButton?.disabled,
    "Location button in connecting state should be present and disabled"
  );

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Ensure the action and location buttons stay vertically stable
 * across the transition through each state (disconnected -> connecting -> connected).
 */
add_task(async function test_buttons_stable_across_state_transitions() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: false,
    bandwidthUsage: mockBandwidthUsage,
  });

  let statusCard = content.statusCardEl;
  let disconnectedActionTop = Math.round(
    statusCard.actionButtonEl.getBoundingClientRect().top
  );
  let disconnectedLocationTop = Math.round(
    content.locationButtonEl.getBoundingClientRect().top
  );

  await setPanelState({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
    isActivating: true,
  });
  await statusCard.updateComplete;

  Assert.equal(
    Math.round(statusCard.actionButtonEl.getBoundingClientRect().top),
    disconnectedActionTop,
    "Action button should not shift when transitioning to connecting"
  );
  Assert.equal(
    Math.round(content.locationButtonEl.getBoundingClientRect().top),
    disconnectedLocationTop,
    "Location button should not shift when transitioning to connecting"
  );

  await setPanelState({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
    isActivating: false,
  });
  await statusCard.updateComplete;

  Assert.equal(
    Math.round(statusCard.actionButtonEl.getBoundingClientRect().top),
    disconnectedActionTop,
    "Action button should not shift when transitioning to connected"
  );
  Assert.equal(
    Math.round(content.locationButtonEl.getBoundingClientRect().top),
    disconnectedLocationTop,
    "Location button should not shift when transitioning to connected"
  );

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Tests the connected state UI.
 */
add_task(async function test_bandwidth_states() {
  const mockUsages = [
    {
      remaining: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
      remainingMB:
        BANDWIDTH.MAX_IN_GB * (BANDWIDTH.BYTES_IN_GB / BANDWIDTH.BYTES_IN_MB),
      remainingGB: BANDWIDTH.MAX_IN_GB,
      max: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
      maxGB: BANDWIDTH.MAX_IN_GB,
      used: 0,
      usedGB: 0,
      percent: "0",
      remainingRounded: BANDWIDTH.MAX_IN_GB,
      gbCount: 2,
      mbCount: 0,
    },
    {
      remaining: 12.1 * BANDWIDTH.BYTES_IN_GB,
      remainingMB: 12.1 * (BANDWIDTH.BYTES_IN_GB / BANDWIDTH.BYTES_IN_MB),
      remainingGB: 12.1,
      max: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
      maxGB: BANDWIDTH.MAX_IN_GB,
      used: 37.9 * BANDWIDTH.BYTES_IN_GB,
      usedGB: 37.9,
      percent: "75",
      remainingRounded: 12.1,
      gbCount: 2,
      mbCount: 0,
    },
    {
      remaining: 4.9 * BANDWIDTH.BYTES_IN_GB,
      remainingMB: 4.9 * (BANDWIDTH.BYTES_IN_GB / BANDWIDTH.BYTES_IN_MB),
      remainingGB: 4.9,
      max: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
      maxGB: BANDWIDTH.MAX_IN_GB,
      used: 45.1 * BANDWIDTH.BYTES_IN_GB,
      usedGB: 45.1,
      percent: "90",
      remainingRounded: 4.9,
      gbCount: 2,
      mbCount: 0,
    },
    {
      remaining: 0.9 * BANDWIDTH.BYTES_IN_GB,
      remainingMB: 0.9 * (BANDWIDTH.BYTES_IN_GB / BANDWIDTH.BYTES_IN_MB),
      remainingGB: 0.9,
      max: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
      maxGB: BANDWIDTH.MAX_IN_GB,
      used: 49.1 * BANDWIDTH.BYTES_IN_GB,
      usedGB: 49.1,
      percent: "90",
      remainingRounded: Math.floor(
        0.9 * (BANDWIDTH.BYTES_IN_GB / BANDWIDTH.BYTES_IN_MB)
      ), // in MB
      gbCount: 1,
      mbCount: 1,
    },
  ];

  for (let mockUsage of mockUsages) {
    await setupStatusCardTest();

    let content = await openPanel({
      location: mockLocation,
      isProtectionEnabled: true,
      bandwidthUsage: mockUsage,
    });

    Assert.ok(
      BrowserTestUtils.isVisible(content),
      "ipprotection content component should be present"
    );

    let statusCard = content.statusCardEl;
    Assert.ok(
      content.statusCardEl,
      "ipprotection-status-card should be present"
    );

    let statusBoxEl = statusCard.statusBoxEl;
    Assert.ok(statusBoxEl, "Status box should be present");

    checkBandwidth(content.bandwidthUsageEl, mockUsage);

    const turnOffVPNButtonEl = statusCard.actionButtonEl;
    Assert.ok(turnOffVPNButtonEl, "Button to turn off VPN should be present");

    await closePanel();
    await cleanupStatusCardTest();
  }
});

/**
 * Tests that clicking the location selection button dispatches the
 * IPProtection:UserShowLocations event.
 */
add_task(async function test_location_button_click_dispatches_event() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
  });

  let locationButton = content.locationButtonEl;
  Assert.ok(locationButton, "Location selection button should be present");

  let showLocationsEventPromise = BrowserTestUtils.waitForEvent(
    window,
    "IPProtection:UserShowLocations"
  );

  locationButton.click();

  await showLocationsEventPromise;
  Assert.ok(true, "IPProtection:UserShowLocations event was dispatched");

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Tests that the location button label shows the chosen country.
 */
add_task(async function test_location_button_label_shows_country() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: "CA",
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
  });

  let locationButton = content.locationButtonEl;
  let label = locationButton.querySelector("[data-l10n-id]");

  Assert.equal(
    label.getAttribute("data-l10n-id"),
    "ipprotection-location-country-button",
    "Country code should select the country l10n id"
  );

  const args = JSON.parse(label.getAttribute("data-l10n-args"));
  const expectedName = countryName("CA");

  Assert.equal(
    args.country,
    expectedName,
    `data-l10n-args.country should be the localized name for CA (got ${args.country})`
  );

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Tests that the location button falls back to recommended when
 * "REC" is selected.
 */
add_task(async function test_location_button_label_recommended_fallback() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: "REC",
    isProtectionEnabled: true,
    bandwidthUsage: mockBandwidthUsage,
  });

  let locationButton = content.locationButtonEl;
  let label = locationButton.querySelector("[data-l10n-id]");

  Assert.equal(
    label.getAttribute("data-l10n-id"),
    "ipprotection-recommended-location-button",
    "REC should keep the recommended l10n id"
  );

  await closePanel();
  await cleanupStatusCardTest();
});

function getBandwidthElement(content) {
  return content.shadowRoot.querySelector(
    "bandwidth-usage, .unlimited-bandwidth"
  );
}

/**
 * Tests that the promo string with the promo end date is shown when the user
 * has unlimited bandwidth without a Mozilla VPN subscription.
 */
add_task(async function test_unlimited_bandwidth_promo() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: null,
    bandwidthUnlimited: true,
    hasUpgraded: false,
  });

  let bandwidthEl = getBandwidthElement(content);

  Assert.ok(bandwidthEl, "Bandwidth element should be rendered");
  Assert.notEqual(
    bandwidthEl.localName,
    "bandwidth-usage",
    "bandwidth-usage should not be rendered"
  );
  Assert.equal(
    bandwidthEl.getAttribute("data-l10n-id"),
    "ipprotection-unlimited-bandwidth-promo",
    "Promo string should be shown"
  );
  Assert.deepEqual(
    JSON.parse(bandwidthEl.getAttribute("data-l10n-args")),
    { endDate: new Date(2027, 0, 31).getTime() },
    "Promo end date should be passed as a timestamp"
  );

  await TestUtils.waitForCondition(
    () => bandwidthEl.textContent,
    "Waiting for the promo string to be localized"
  );

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Tests that the upgraded string is shown when the user has unlimited
 * bandwidth from a Mozilla VPN subscription.
 */
add_task(async function test_unlimited_bandwidth_upgraded() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: null,
    bandwidthUnlimited: true,
    hasUpgraded: true,
  });

  let bandwidthEl = getBandwidthElement(content);

  Assert.ok(bandwidthEl, "Bandwidth element should be rendered");
  Assert.equal(
    bandwidthEl.getAttribute("data-l10n-id"),
    "ipprotection-unlimited-bandwidth-upgraded",
    "Upgraded string should be shown"
  );
  Assert.ok(
    !bandwidthEl.hasAttribute("data-l10n-args"),
    "Upgraded string should not have l10n args"
  );

  await closePanel();
  await cleanupStatusCardTest();
});

/**
 * Tests that no bandwidth element is rendered when there is no usage
 * data and the user does not have unlimited bandwidth.
 */
add_task(async function test_no_bandwidth_usage_not_unlimited() {
  await setupStatusCardTest();

  let content = await openPanel({
    location: mockLocation,
    isProtectionEnabled: true,
    bandwidthUsage: null,
    bandwidthUnlimited: false,
    hasUpgraded: false,
  });

  Assert.ok(
    !getBandwidthElement(content),
    "No bandwidth element should be rendered"
  );

  await closePanel();
  await cleanupStatusCardTest();
});
