/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { BANDWIDTH, LINKS } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);
const { ERRORS } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs"
);
const lazy = {};

const mockBandwidthUsage = {
  remaining: 15 * BANDWIDTH.BYTES_IN_GB,
  max: BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB,
};

add_task(async function test_paused_content() {
  setupService({
    isReady: true,
    canEnroll: true,
    proxyPass: {
      status: 200,
      error: undefined,
      pass: makePass(),
    },
  });

  let { promise, resolve } = Promise.withResolvers();
  let refreshStub = sinon
    .stub(IPPProxyManager, "refreshUsage")
    .callsFake(() => promise);

  let content = await openPanel({
    paused: true,
    hasUpgraded: false,
    bandwidthUsage: mockBandwidthUsage,
  });
  resolve();
  refreshStub.restore();

  await TestUtils.waitForCondition(
    () => content.statusBoxEl,
    "Status box should be shown when paused"
  );

  let statusBox = content.statusBoxEl;
  Assert.ok(statusBox, "Status box should be shown when paused");

  let pausedTitle = statusBox.titleEl;
  let pausedDescription = statusBox.descriptionEl;
  let upgradeContent = content.upgradeEl;
  let upgradeDescription = upgradeContent.querySelector(
    "#upgrade-vpn-description"
  );
  let upgradeButton = upgradeContent.querySelector("#upgrade-vpn-button");

  Assert.ok(pausedTitle, "Paused title should be present");
  Assert.ok(pausedDescription, "Paused description should be present");
  await checkStatusBoxAriaLabel(statusBox);
  Assert.ok(
    upgradeContent,
    "Upgrade content should be present when not upgraded"
  );
  Assert.ok(upgradeDescription, "Upgrade description should be present");
  Assert.ok(
    upgradeButton,
    "Upgrade button should be present when not upgraded"
  );
  Assert.ok(!content.statusCardEl, "Status card should be hidden when paused");

  let newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    LINKS.PRODUCT_URL + "#pricing"
  );
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  upgradeButton.click();
  let newTab = await newTabPromise;
  await panelHiddenPromise;

  Assert.equal(
    gBrowser.selectedTab,
    newTab,
    "New tab is now open in a new foreground tab"
  );

  await setPanelState();
  IPProtection.getPanel(window).initiatedUpgrade = false;
  BrowserTestUtils.removeTab(newTab);
  cleanupService();
});

add_task(async function test_paused_content_upgraded() {
  setupService({
    isReady: true,
    hasUpgraded: true,
    canEnroll: true,
    proxyPass: {
      status: 200,
      error: undefined,
      pass: makePass(),
    },
  });

  let { promise, resolve } = Promise.withResolvers();
  let refreshStub = sinon
    .stub(IPPProxyManager, "refreshUsage")
    .callsFake(() => promise);

  let content = await openPanel({
    paused: true,
    hasUpgraded: true,
    bandwidthUsage: mockBandwidthUsage,
  });
  resolve();
  refreshStub.restore();

  await TestUtils.waitForCondition(
    () => content.statusBoxEl,
    "Status box should be shown when paused"
  );

  let statusBox = content.statusBoxEl;
  Assert.ok(statusBox, "Status box should be shown when paused");

  let pausedTitle = statusBox.titleEl;
  let pausedDescription = statusBox.descriptionEl;
  let upgradeContent = content.upgradeEl;

  Assert.ok(pausedTitle, "Paused title should be present");
  Assert.ok(pausedDescription, "Paused description should be present");
  await checkStatusBoxAriaLabel(statusBox);
  Assert.ok(
    !upgradeContent,
    "Upgrade content should not be present when user has upgraded"
  );
  Assert.ok(!content.statusCardEl, "Status card should be hidden when paused");

  await setPanelState();
  await closePanel();
  cleanupService();
});

/**
 * Tests that opening the panel while paused re-checks usage, showing the
 * paused screen immediately (not a loading state) while the refresh is in
 * flight and keeping it once the refresh completes.
 */
add_task(async function test_showing_refreshes_usage_when_paused() {
  setupService({
    isReady: true,
    hasUpgraded: true,
    canEnroll: true,
    proxyPass: {
      status: 200,
      error: undefined,
      pass: makePass(),
    },
  });

  let { promise, resolve } = Promise.withResolvers();
  let refreshStub = sinon
    .stub(IPPProxyManager, "refreshUsage")
    .callsFake(() => promise);

  let content = await openPanel({
    paused: true,
    hasUpgraded: true,
    bandwidthUsage: mockBandwidthUsage,
  });

  Assert.ok(
    refreshStub.calledOnce,
    "Usage should be refreshed when opening the panel while paused"
  );
  Assert.ok(
    content.statusBoxEl,
    "Paused screen should be shown immediately while usage is refreshing"
  );
  Assert.ok(
    !content.shadowRoot.querySelector("#enrolling-container"),
    "Loading state should not be shown while usage is refreshing"
  );

  resolve();
  refreshStub.restore();

  await TestUtils.waitForCondition(
    () => content.statusBoxEl,
    "Paused screen should be shown once the usage refresh completes"
  );

  await setPanelState();
  await closePanel();
  cleanupService();
});

/**
 * Tests the generic error type.
 */
add_task(async function test_generic_error() {
  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.GENERIC,
  });

  let statusBox = content.statusBoxEl;
  Assert.ok(statusBox, "Status box should be shown when there is an error");

  let errorTitle = statusBox.titleEl;
  let errorDescription = statusBox.descriptionEl;

  Assert.ok(errorTitle, "Error title should be present");
  Assert.ok(errorDescription, "Error description should be present");
  await checkStatusBoxAriaLabel(statusBox);

  Assert.equal(
    statusBox.type,
    ERRORS.GENERIC,
    "Status box type should be generic-error"
  );

  Assert.ok(!content.statusCardEl, "Status card should be hidden when error");

  let footerButton = content.settingsButtonEl;
  Assert.ok(footerButton, "Settings button should be present in footer");

  await closePanel();
});

/**
 * Tests the network error type in the status box component.
 */
add_task(async function test_network_error() {
  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.NETWORK,
  });

  let statusBox = content.statusBoxEl;
  Assert.ok(statusBox, "Status box should be shown when there is an error");

  let errorTitle = statusBox.titleEl;
  let errorDescription = statusBox.descriptionEl;

  Assert.ok(errorTitle, "Error title should be present");
  Assert.ok(errorDescription, "Error description should be present");
  await checkStatusBoxAriaLabel(statusBox);

  Assert.equal(
    statusBox.type,
    ERRORS.NETWORK,
    "Status box type should be network-error"
  );

  // Check for the error icon in the network error case
  let errorImage = statusBox.querySelector('img[slot="image"]');
  Assert.ok(errorImage, "Error icon should be present for network error");

  Assert.ok(!content.statusCardEl, "Status card should be hidden when error");

  let footerButton = content.settingsButtonEl;
  Assert.ok(footerButton, "Settings button should be present in footer");

  await closePanel();
});

/**
 * Tests the catastrophic error type in the status box component.
 */
add_task(async function test_catastrophic_error() {
  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.CATASTROPHIC,
  });

  let statusBox = content.statusBoxEl;
  Assert.ok(statusBox, "Status box should be shown when there is an error");

  let errorTitle = statusBox.titleEl;
  let errorDescription = statusBox.descriptionEl;

  Assert.ok(errorTitle, "Error title should be present");
  Assert.ok(errorDescription, "Error description should be present");
  await checkStatusBoxAriaLabel(statusBox);

  Assert.equal(
    statusBox.type,
    ERRORS.CATASTROPHIC,
    "Status box type should be catastrophic-error"
  );

  let errorImage = statusBox.querySelector('img[slot="image"]');
  Assert.ok(errorImage, "Error icon should be present for catastrophic error");

  Assert.ok(!content.statusCardEl, "Status card should be hidden when error");

  let footerButton = content.settingsButtonEl;
  Assert.ok(footerButton, "Settings button should be present in footer");

  await closePanel();
});

/**
 * Tests the locale unavailable error type in the status box component.
 */
add_task(async function test_unavailable_error() {
  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.VPN_UNAVAILABLE,
  });

  let statusBox = content.statusBoxEl;
  Assert.ok(statusBox, "Status box should be shown when there is an error");

  let errorTitle = statusBox.titleEl;
  let errorDescription = statusBox.descriptionEl;
  let supportLink = errorDescription.querySelector("a");

  Assert.ok(errorTitle, "Error title should be present");
  Assert.ok(errorDescription, "Error description should be present");
  Assert.ok(supportLink, "Support link should be present");
  await checkStatusBoxAriaLabel(statusBox);
  Assert.equal(
    supportLink.href,
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
      LINKS.NO_ACCESS_SUPPORT_SLUG,
    "Correct support link should be used"
  );

  Assert.equal(
    statusBox.type,
    ERRORS.VPN_UNAVAILABLE,
    "Status box type should be vpn-unavailable"
  );

  Assert.ok(!content.statusCardEl, "Status card should be hidden when error");

  let footerButton = content.settingsButtonEl;
  Assert.ok(footerButton, "Settings button should be present in footer");

  await closePanel();
});

/**
 * Tests that clicking the support link in the unavailable error closes the panel.
 */
add_task(async function test_unavailable_error_support_link_closes_panel() {
  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.VPN_UNAVAILABLE,
  });

  let supportLink = content.statusBoxEl.descriptionEl.querySelector("a");
  Assert.ok(supportLink, "Support link should be present");

  let newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
      LINKS.NO_ACCESS_SUPPORT_SLUG
  );
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  supportLink.click();
  info("after click");
  let newTab = await newTabPromise;
  info("after newTabPromise");
  await panelHiddenPromise;
  info("afterpanelHiddenPromise");
  await setPanelState();
  info("after setPanelState");
  BrowserTestUtils.removeTab(newTab);
});

/**
 * Tests that failing to activate because the region is unavailable shows the
 * generic error rather than the unavailable error.
 */
add_task(async function test_region_unavailable_shows_generic_error() {
  setupService({
    isReady: true,
    proxyPass: {
      status: 451,
      error: "region_unavailable",
      pass: null,
    },
  });
  IPProtectionService.updateState();
  await waitForProxyState(IPPProxyStates.READY);

  const sandbox = sinon.createSandbox();
  let startSpy = sandbox.spy(IPPProxyManager, "start");

  let content = await openPanel({
    isReady: true,
  });

  let turnOnButton = content.statusCardEl?.actionButtonEl;
  Assert.ok(turnOnButton, "Turn on button should be present");
  turnOnButton.click();

  await TestUtils.waitForCondition(
    () => content.state.error === ERRORS.GENERIC,
    "Panel should show generic error after failed activation"
  );
  await content.updateComplete;

  let { error } = await startSpy.firstCall.returnValue;
  Assert.equal(
    error,
    ERRORS.VPN_UNAVAILABLE,
    "Activation should fail with the unavailable error"
  );
  Assert.equal(
    content.statusBoxEl?.type,
    ERRORS.GENERIC,
    "Status box type should be generic-error"
  );

  await closePanel();
  sandbox.restore();
  cleanupService();
});
