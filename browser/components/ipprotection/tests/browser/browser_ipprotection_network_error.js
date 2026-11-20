/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { ERRORS } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPProxyManager.sys.mjs"
);
const { IPPNetworkUtils } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPNetworkUtils.sys.mjs"
);

/**
 * Tests that the panel does not show the network error when opened while offline.
 * Error should only appear after attempting to activate the VPN.
 */
add_task(async function test_panel_no_error_when_opened_offline() {
  setupService({
    isReady: true,
    canEnroll: true,
    proxyPass: {
      status: 200,
      error: undefined,
      pass: makePass(),
    },
  });

  // Go offline before opening panel
  Services.io.offline = true;

  let content = await openPanel({
    unauthenticated: false,
  });

  await content.updateComplete;

  Assert.ok(
    !content.state.error,
    "Network error should not be present when opened while offline"
  );
  Assert.ok(
    !content.statusBoxEl,
    "Status box should not be present when opened while offline"
  );

  // Should show normal status card with Turn On button
  let statusCard = content.statusCardEl;
  Assert.ok(statusCard, "Status card should be present");

  let turnOnButton = statusCard.actionButtonEl;
  Assert.ok(turnOnButton, "Turn on button should be present");

  // Cleanup - go back online
  Services.io.offline = false;

  await closePanel();
  cleanupService();
});

/**
 * Tests that the toolbar button shows an error icon after the panel fails
 * to activate due to network issues, and resets when the panel closes.
 */
add_task(async function test_toolbar_button_icon_on_activation_failure() {
  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();
  await waitForProxyState(IPPProxyStates.READY);

  let button = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(button, "Toolbar button should exist");

  // Initially should not be in error state
  Assert.ok(
    !button.classList.contains("ipprotection-error"),
    "Toolbar button should not show error initially"
  );

  // Go offline
  Services.io.offline = true;

  Assert.ok(
    !button.classList.contains("ipprotection-error"),
    "Toolbar button should not show error from offline status change alone"
  );

  // Open panel and try to activate while offline
  let content = await openPanel({
    isReady: true,
  });
  let turnOnButton = content.statusCardEl?.actionButtonEl;
  Assert.ok(turnOnButton, "Turn on button should be present");

  turnOnButton.click();

  // Wait for panel to show network error
  await TestUtils.waitForCondition(
    () => content.state.error === ERRORS.NETWORK,
    "Panel should show network error after failed activation"
  );

  Assert.ok(
    button.classList.contains("ipprotection-network-error"),
    "Toolbar button should show network error icon after panel activation failure"
  );

  // Back online
  Services.io.offline = false;
  // Close the panel
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHiddenPromise;

  Assert.ok(
    !button.classList.contains("ipprotection-network-error"),
    "Toolbar button should clear network error icon when panel closes"
  );

  cleanupService();
});

/**
 * Tests that network errors are caught when trying to activate VPN while offline.
 */
add_task(async function test_network_error_when_activating_offline() {
  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();
  await waitForProxyState(IPPProxyStates.READY);

  // Go offline before opening panel
  Services.io.offline = true;

  let content = await openPanel({
    isReady: true,
  });

  await content.updateComplete;

  let statusCard = content.statusCardEl;
  Assert.ok(statusCard, "Status card should be present");

  let turnOnButton = statusCard.actionButtonEl;
  Assert.ok(turnOnButton, "Turn on button should be present");

  // Try to activate the VPN while offline
  turnOnButton.click();

  // Wait for panel to show the network error (set directly by #startProxy)
  await TestUtils.waitForCondition(
    () => content.state.error === ERRORS.NETWORK,
    "Panel should show network error after failed activation"
  );

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "IPPProxyManager should stay in READY state when activation fails while offline"
  );

  await content.updateComplete;

  let statusBox = content.statusBoxEl;
  Assert.ok(statusBox, "Status box should be shown for error");
  Assert.equal(
    content.state.error,
    ERRORS.NETWORK,
    "Panel should show network error"
  );

  // Cleanup - go back online
  Services.io.offline = false;

  await closePanel();
  cleanupService();
});

/**
 * Tests that switching to offline mode while the VPN is on shows the network
 * error, and that turning the VPN off afterwards clears it.
 */
add_task(async function test_network_error_work_offline_while_active() {
  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();
  await waitForProxyState(IPPProxyStates.READY);

  let content = await openPanel({
    isReady: true,
  });

  content.statusCardEl.actionButtonEl.click();
  await waitForProxyState(IPPProxyStates.ACTIVE);

  Services.io.offline = true;

  await TestUtils.waitForCondition(
    () => content.state.error === ERRORS.NETWORK,
    "Panel should show network error after choosing Work Offline"
  );
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ERROR,
    "IPPProxyManager should move to ERROR when choosing Work Offline while active"
  );
  Assert.equal(
    IPPProxyManager.errorType,
    ERRORS.NETWORK,
    "The proxy error should be a network error"
  );

  await IPPProxyManager.stop();

  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "IPPProxyManager should be READY after stopping"
  );
  Assert.ok(!content.state.error, "Panel error should be cleared");

  Services.io.offline = false;

  await closePanel();
  cleanupService();
});

/**
 * Brings the proxy up for an included site, then selects a tab showing url.
 *
 * @param {string} includedSite
 * @param {string} url
 * @returns {Promise<{principal: nsIPrincipal, tabs: Array}>}
 */
async function setupInclusionMode(includedSite, url) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.features.siteInclusions", true]],
  });

  setupService({
    isReady: true,
  });

  // The tab goes first: an inclusion brings the proxy up, and the included
  // site would then load through it.
  let tabs = [
    await BrowserTestUtils.openNewForegroundTab(gBrowser, includedSite),
  ];

  const principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      includedSite
    );
  IPPPermissionRules.setRule(principal, IPPPrincipalRules.INCLUDED);

  await waitForProxyState(IPPProxyStates.ACTIVE);
  Assert.equal(
    IPPProxyManager.mode,
    IPPMode.MODE_INCLUSION,
    "The proxy should be up for included sites only"
  );

  if (url !== includedSite) {
    tabs.push(await BrowserTestUtils.openNewForegroundTab(gBrowser, url));
  }
  return { principal, tabs };
}

async function cleanupInclusionMode(principal, tabs) {
  await IPPProxyManager.stop();
  Services.io.offline = false;

  await closePanel();
  for (let tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
  IPPPermissionRules.setRule(principal, IPPPrincipalRules.DEFAULT);
  await waitForProxyState(IPPProxyStates.READY);
  cleanupService();
  await SpecialPowers.popPrefEnv();
}

/**
 * Tests that switching to offline mode while on an included site, with the
 * proxy only up for included sites, shows the network error.
 */
add_task(async function test_network_error_work_offline_inclusion_mode() {
  const includedSite = "https://example.com";
  let { principal, tabs } = await setupInclusionMode(
    includedSite,
    includedSite
  );
  let content = await openPanel();

  Services.io.offline = true;

  await TestUtils.waitForCondition(
    () => content.state.error === ERRORS.NETWORK,
    "Panel should show network error after choosing Work Offline"
  );
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ERROR,
    "IPPProxyManager should move to ERROR when choosing Work Offline in inclusion mode"
  );
  Assert.equal(
    IPPProxyManager.errorType,
    ERRORS.NETWORK,
    "The proxy error should be a network error"
  );

  await cleanupInclusionMode(principal, tabs);
});

/**
 * Tests that switching to offline mode with the included site in a background
 * tab shows the network error once that tab is selected.
 */
add_task(async function test_network_error_work_offline_background_tab() {
  const includedSite = "https://example.com";
  let { principal, tabs } = await setupInclusionMode(
    includedSite,
    "https://example.org"
  );

  Services.io.offline = true;

  await waitForProxyState(IPPProxyStates.ERROR);

  let content = await openPanel();
  Assert.ok(
    !content.state.error,
    "Panel should not show an error on a site that is not included"
  );
  await closePanel();

  await BrowserTestUtils.switchTab(gBrowser, tabs[0]);

  content = await openPanel();
  await TestUtils.waitForCondition(
    () => content.state.error === ERRORS.NETWORK,
    "Panel should show network error on the included site"
  );

  await cleanupInclusionMode(principal, tabs);
});

/**
 * Tests that the proxy for included sites comes back up after leaving
 * offline mode.
 */
add_task(async function test_inclusion_mode_recovers_after_offline() {
  const includedSite = "https://example.com";
  let { principal, tabs } = await setupInclusionMode(
    includedSite,
    includedSite
  );
  let content = await openPanel();

  Services.io.offline = true;
  await waitForProxyState(IPPProxyStates.ERROR);

  Services.io.offline = false;
  await waitForProxyState(IPPProxyStates.ACTIVE);
  Assert.equal(
    IPPProxyManager.mode,
    IPPMode.MODE_INCLUSION,
    "The proxy should be back up for included sites"
  );
  Assert.ok(!content.state.error, "Panel error should be cleared");

  await cleanupInclusionMode(principal, tabs);
});

/**
 * Tests that adding an inclusion while offline waits to bring the proxy up
 * until going back online.
 */
add_task(async function test_add_inclusion_while_offline() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.features.siteInclusions", true]],
  });

  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();
  await waitForProxyState(IPPProxyStates.READY);

  Services.io.offline = true;

  const principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://example.com"
    );
  IPPPermissionRules.setRule(principal, IPPPrincipalRules.INCLUDED);

  await TestUtils.waitForTick();
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.READY,
    "The proxy should not start for included sites while offline"
  );

  Services.io.offline = false;

  await waitForProxyState(IPPProxyStates.ACTIVE);
  Assert.equal(
    IPPProxyManager.mode,
    IPPMode.MODE_INCLUSION,
    "The proxy should come up for included sites once back online"
  );

  IPPPermissionRules.setRule(principal, IPPPrincipalRules.DEFAULT);
  await waitForProxyState(IPPProxyStates.READY);
  cleanupService();
  await SpecialPowers.popPrefEnv();
});
