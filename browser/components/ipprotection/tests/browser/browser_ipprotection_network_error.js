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

/**
 * Waits for the error page in a browser and returns its title l10n id. Call
 * it before the load that ends on the error page.
 *
 * @param {MozBrowser} browser
 * @returns {Promise<string>}
 */
async function waitForErrorPageTitle(browser) {
  await BrowserTestUtils.waitForErrorPage(browser);
  return SpecialPowers.spawn(browser, [], async () => {
    const card = await ContentTaskUtils.waitForCondition(
      () => content.document.querySelector("net-error-card")?.wrappedJSObject,
      "net-error-card should be present"
    );
    await card.getUpdateComplete();
    return card.shadowRoot
      .querySelector("#error-title")
      .getAttribute("data-l10n-id");
  });
}

function clickCardButton(tab, id) {
  return SpecialPowers.spawn(tab.linkedBrowser, [id], buttonId => {
    content.document
      .querySelector("net-error-card")
      .shadowRoot.getElementById(buttonId)
      .click();
  });
}

/**
 * Sets up the service for a VPN error page test. Everything it changes, and
 * every tab opened through it, is undone on dispose:
 *   await using test = await setupVPNErrorTest();
 *
 * @param {object} [options]
 * @param {boolean} [options.failingPass] - proxy passes fail, so an
 *   activation ends in ERROR
 * @param {string[]} [options.includedSites] - origins with an inclusion rule
 * @param {Array} [options.prefs] - extra prefs, set once the proxy settled
 * @returns {Promise<{
 *   openErrorPage: (url: string) => Promise<{tab: MozTabbrowserTab, title: string}>,
 * }>}
 */
async function setupVPNErrorTest({
  failingPass = false,
  includedSites = [],
  prefs = [],
} = {}) {
  let pushedPrefs = 0;
  const pushPrefs = async set => {
    await SpecialPowers.pushPrefEnv({ set });
    pushedPrefs++;
  };
  const tabs = [];
  const principals = includedSites.map(site =>
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(site)
  );

  // Forget the pass an earlier task left behind, so the next one is fetched.
  await IPPProxyManager.reset();
  setupService({
    isReady: true,
    ...(failingPass && {
      proxyPass: { status: 500, error: "test_error", pass: undefined },
    }),
  });
  IPProtectionService.updateState();
  await waitForProxyState(IPPProxyStates.READY);

  if (principals.length) {
    await pushPrefs([["browser.ipProtection.features.siteInclusions", true]]);
    for (const principal of principals) {
      IPPPermissionRules.setRule(principal, IPPPrincipalRules.INCLUDED);
    }
    await waitForProxyState(
      failingPass ? IPPProxyStates.ERROR : IPPProxyStates.ACTIVE
    );
  }
  if (prefs.length) {
    await pushPrefs(prefs);
  }

  return {
    async openErrorPage(url) {
      const tab = BrowserTestUtils.addTab(gBrowser, url);
      tabs.push(tab);
      return { tab, title: await waitForErrorPageTitle(tab.linkedBrowser) };
    },

    async [Symbol.asyncDispose]() {
      for (const tab of tabs) {
        BrowserTestUtils.removeTab(tab);
      }
      for (const principal of principals) {
        IPPPermissionRules.setRule(principal, IPPPrincipalRules.DEFAULT);
      }
      await IPPProxyManager.stop(false);
      await waitForProxyState(IPPProxyStates.READY);
      await cleanupService();
      for (; pushedPrefs; pushedPrefs--) {
        await SpecialPowers.popPrefEnv();
      }
    },
  };
}

/**
 * Prefs routing https through a manual proxy that refuses connections.
 *
 * @returns {Array}
 */
function brokenManualProxyPrefs() {
  const socket = Cc["@mozilla.org/network/server-socket;1"].createInstance(
    Ci.nsIServerSocket
  );
  socket.init(-1, true, -1);
  const closedPort = socket.port;
  socket.close();
  return [
    ["network.proxy.type", 1],
    ["network.proxy.ssl", "127.0.0.1"],
    ["network.proxy.ssl_port", closedPort],
  ];
}

/**
 * Tests that a page loaded while the proxy is in ERROR shows the VPN error
 * page, even when the user never turned the VPN on.
 */
add_task(async function test_error_state_shows_vpn_error_page() {
  await using test = await setupVPNErrorTest({ failingPass: true });
  await IPPProxyManager.start(false);
  Assert.equal(
    IPPProxyManager.state,
    IPPProxyStates.ERROR,
    "A failed system activation should stay in ERROR"
  );

  const { title } = await test.openErrorPage("https://example.com/");
  Assert.equal(
    title,
    "fp-neterror-vpn-error-title",
    "The VPN error page should be shown"
  );
});

/**
 * Tests that continuing loads the failed site without the VPN, only for that
 * site, only in that tab, and only until the tab leaves the site.
 */
add_task(async function test_continue_skips_vpn_for_site_in_tab() {
  const site = "https://example.com/";
  const otherSite = "https://example.org/";
  await using test = await setupVPNErrorTest({
    failingPass: true,
    includedSites: ["https://example.com", "https://example.org"],
  });

  const { tab } = await test.openErrorPage(site);
  const loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, site);
  await clickCardButton(tab, "continueWithoutVPNButton");
  await loaded;
  Assert.equal(
    tab.linkedBrowser.documentURI.spec,
    site,
    "The site should load in the tab it was continued in"
  );

  const otherSiteError = waitForErrorPageTitle(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, otherSite);
  Assert.equal(
    await otherSiteError,
    "fp-neterror-vpn-error-title",
    "Another site in the tab should still need the VPN"
  );

  const backOnSiteError = waitForErrorPageTitle(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, site);
  Assert.equal(
    await backOnSiteError,
    "fp-neterror-vpn-error-title",
    "Leaving the site should end skipping the VPN for it"
  );

  const { title } = await test.openErrorPage(site);
  Assert.equal(
    title,
    "fp-neterror-vpn-error-title",
    "Another tab should still need the VPN for the site"
  );
});

/**
 * Tests that trying again restarts the failed connection in its mode.
 */
add_task(async function test_try_again_restarts_connection_in_its_mode() {
  await using test = await setupVPNErrorTest({
    failingPass: true,
    includedSites: ["https://example.com"],
  });
  const { tab, title } = await test.openErrorPage("https://example.com/");
  Assert.equal(
    title,
    "fp-neterror-vpn-error-title",
    "The VPN error page should be shown"
  );

  setupService({
    isReady: true,
    proxyPass: { status: 200, error: undefined, pass: makePass() },
  });
  const active = waitForProxyState(IPPProxyStates.ACTIVE);
  await clickCardButton(tab, "tryAgainButton");
  await active;
  Assert.equal(
    IPPProxyManager.mode,
    IPPMode.MODE_INCLUSION,
    "The retry should keep the inclusion mode"
  );
});

/**
 * Tests that trying again while the VPN still fails keeps the traffic held
 * and the VPN error page shown.
 */
add_task(async function test_failed_try_again_keeps_error_page() {
  await using test = await setupVPNErrorTest({
    failingPass: true,
    includedSites: ["https://example.com"],
  });
  const { tab } = await test.openErrorPage("https://example.com/");

  const activating = waitForProxyState(IPPProxyStates.ACTIVATING);
  const failed = activating.then(() => waitForProxyState(IPPProxyStates.ERROR));
  await clickCardButton(tab, "tryAgainButton");
  await failed;

  const reloaded = waitForErrorPageTitle(tab.linkedBrowser);
  tab.linkedBrowser.reload();
  Assert.equal(
    await reloaded,
    "fp-neterror-vpn-error-title",
    "The site should still not load without the VPN"
  );
});

/**
 * Tests that a proxy failure on a site IP Protection does not carry keeps the
 * plain proxy error page, even while the VPN is up for other sites.
 */
add_task(async function test_unrelated_proxy_failure_keeps_proxy_error_page() {
  await using test = await setupVPNErrorTest({
    includedSites: ["https://example.com"],
    prefs: brokenManualProxyPrefs(),
  });
  Assert.equal(
    IPPProxyManager.mode,
    IPPMode.MODE_INCLUSION,
    "The proxy should be up for included sites only"
  );

  const { title } = await test.openErrorPage("https://example.org/");
  Assert.equal(
    title,
    "proxyConnectFailure-title",
    "The plain proxy error page should be shown"
  );
});
