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

  Assert.ok(errorTitle, "Error title should be present");
  Assert.equal(
    errorTitle.getAttribute("data-l10n-id"),
    "ipprotection-connection-status-blocked-error-title-1",
    "Error title should say the VPN is unavailable"
  );
  Assert.ok(!statusBox.descriptionEl, "Error description should not be shown");
  await checkStatusBoxAriaLabel(statusBox);

  Assert.equal(
    statusBox.type,
    ERRORS.GENERIC,
    "Status box type should be generic-error"
  );

  Assert.equal(
    statusBox.querySelector('img[slot="image"]')?.src,
    "chrome://browser/content/ipprotection/assets/states/ipprotection-error.svg",
    "Error icon should be shown"
  );

  let tryAgainButton = content.tryAgainButtonEl;
  Assert.ok(tryAgainButton, "Try again button should be present");
  Assert.equal(
    tryAgainButton.type,
    "primary",
    "Try again button should be a primary button"
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
  Assert.ok(
    !content.tryAgainButtonEl,
    "Try again button should only be shown for the generic error"
  );

  Assert.ok(!content.statusCardEl, "Status card should be hidden when error");

  let footerButton = content.settingsButtonEl;
  Assert.ok(footerButton, "Settings button should be present in footer");

  await closePanel();
});

/**
 * Tests that the catastrophic error shows the same state as the generic error.
 */
add_task(async function test_catastrophic_error() {
  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.CATASTROPHIC,
  });

  let statusBox = content.statusBoxEl;
  Assert.ok(statusBox, "Status box should be shown when there is an error");

  let errorTitle = statusBox.titleEl;

  Assert.ok(errorTitle, "Error title should be present");
  Assert.equal(
    errorTitle.getAttribute("data-l10n-id"),
    "ipprotection-connection-status-blocked-error-title-1",
    "Error title should have correct l10n id"
  );
  Assert.ok(!statusBox.descriptionEl, "Error description should not be shown");
  await checkStatusBoxAriaLabel(statusBox);

  Assert.equal(
    statusBox.type,
    ERRORS.GENERIC,
    "Status box type should be generic-error"
  );

  Assert.equal(
    statusBox.querySelector('img[slot="image"]')?.src,
    "chrome://browser/content/ipprotection/assets/states/ipprotection-error.svg",
    "Error icon should be shown"
  );
  Assert.ok(content.tryAgainButtonEl, "Try again button should be present");

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

/**
 * Tests that clicking Try again on the generic error connects the VPN.
 */
add_task(async function test_generic_error_try_again_connects() {
  setupService({
    isReady: true,
  });
  IPProtectionService.updateState();
  await waitForProxyState(IPPProxyStates.READY);

  let content = await openPanel({
    isReady: true,
    error: ERRORS.GENERIC,
  });

  let tryAgainButton = content.tryAgainButtonEl;
  Assert.ok(tryAgainButton, "Try again button should be present");
  tryAgainButton.click();

  await waitForProxyState(IPPProxyStates.ACTIVE);
  await TestUtils.waitForCondition(
    () => !content.state.error && content.statusCardEl,
    "Panel should show the status card once connected"
  );
  Assert.ok(content.state.isActive, "Panel state should be active");
  Assert.ok(!content.statusBoxEl, "Error should no longer be shown");

  await closePanel();
  await IPPProxyManager.reset();
  cleanupService();
});

/**
 * Tests that a failed Try again leaves the generic error unchanged.
 */
add_task(async function test_generic_error_try_again_fails() {
  // Drop any pass cached by an earlier connection so that starting fetches
  // a new one and hits the failing response.
  await IPPProxyManager.reset();
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
    error: ERRORS.GENERIC,
  });

  let statusCardShown = false;
  let observer = new MutationObserver(() => {
    if (content.statusCardEl) {
      statusCardShown = true;
    }
  });
  observer.observe(content.shadowRoot, { childList: true, subtree: true });

  content.tryAgainButtonEl.click();

  await TestUtils.waitForCondition(
    () => startSpy.called,
    "Retrying should try to start the proxy"
  );
  let { error } = await startSpy.firstCall.returnValue;
  Assert.ok(error, "Starting the proxy should fail");
  await content.updateComplete;
  observer.disconnect();

  Assert.equal(
    content.state.error,
    ERRORS.GENERIC,
    "Panel should still show the generic error"
  );
  Assert.equal(
    content.statusBoxEl?.type,
    ERRORS.GENERIC,
    "Status box should still be the generic error"
  );
  Assert.ok(content.tryAgainButtonEl, "Try again button should still be shown");
  Assert.ok(!statusCardShown, "Status card should not be shown while retrying");

  await closePanel();
  sandbox.restore();
  await IPPProxyManager.stop();
  cleanupService();
});

/**
 * Tests that the generic error shows both Try again and Continue without VPN
 * when the VPN is on.
 */
add_task(async function test_generic_error_with_vpn_on_shows_both_buttons() {
  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.GENERIC,
    isDefaultMode: true,
  });

  try {
    let tryAgainButton = content.tryAgainButtonEl;
    let continueButton = content.continueWithoutVpnButtonEl;
    Assert.ok(tryAgainButton, "Try again button should be present");
    Assert.ok(continueButton, "Continue without VPN button should be present");

    Assert.ok(
      tryAgainButton.compareDocumentPosition(continueButton) &
        Node.DOCUMENT_POSITION_FOLLOWING,
      "Continue without VPN button should come after Try again"
    );
  } finally {
    await closePanel();
  }
});

/**
 * Tests that the generic error doesn't show Continue without VPN when the VPN
 * is off.
 */
add_task(async function test_generic_error_with_vpn_off_hides_continue() {
  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.GENERIC,
    isDefaultMode: false,
  });

  Assert.ok(content.tryAgainButtonEl, "Try again button should be present");
  Assert.ok(
    !content.continueWithoutVpnButtonEl,
    "Continue without VPN button should not be present"
  );

  await closePanel();
});

/**
 * Tests that the network and unavailable errors don't show either button.
 */
add_task(async function test_network_and_unavailable_errors_hide_buttons() {
  for (let error of [ERRORS.NETWORK, ERRORS.VPN_UNAVAILABLE]) {
    let content = await openPanel({
      unauthenticated: false,
      error,
      isDefaultMode: true,
    });

    Assert.ok(
      !content.tryAgainButtonEl,
      `Try again button should not be present for ${error}`
    );
    Assert.ok(
      !content.continueWithoutVpnButtonEl,
      `Continue without VPN button should not be present for ${error}`
    );

    await closePanel();
  }
});

/**
 * Opens the panel on a catastrophic error with site inclusions enabled and
 * checks that the error screen replaces the site rule UI.
 *
 * @param {object} options
 * @param {boolean} options.isInclusion Whether the site is included, else excluded.
 * @param {boolean} options.isProtectionEnabled Whether the VPN is on.
 * @returns {Promise<IPProtectionContentElement>}
 */
async function openCatastrophicErrorOnSite({
  isInclusion,
  isProtectionEnabled,
}) {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.features.siteInclusions", true]],
  });
  return await openPanel({
    unauthenticated: false,
    error: ERRORS.CATASTROPHIC,
    isSiteInclusionsEnabled: true,
    isActive: true,
    isProtectionEnabled,
    isDefaultMode: isProtectionEnabled,
    siteData: {
      host: "example.com",
      isInclusion,
      isExclusion: !isInclusion,
      hasSiteRule: true,
    },
  });
}

/**
 * Tests a catastrophic error on an included site while the VPN is off.
 */
add_task(async function test_catastrophic_error_included_site_vpn_off() {
  let content = await openCatastrophicErrorOnSite({
    isInclusion: true,
    isProtectionEnabled: false,
  });

  Assert.ok(content.tryAgainButtonEl, "Try again button should be present");
  Assert.ok(
    !content.continueWithoutVpnButtonEl,
    "Continue without VPN button should not be present"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests a catastrophic error on an included site while the VPN is on.
 */
add_task(async function test_catastrophic_error_included_site_vpn_on() {
  let content = await openCatastrophicErrorOnSite({
    isInclusion: true,
    isProtectionEnabled: true,
  });

  Assert.ok(content.tryAgainButtonEl, "Try again button should be present");
  Assert.ok(
    content.continueWithoutVpnButtonEl,
    "Continue without VPN button should be present"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests a catastrophic error on an excluded site while the VPN is on.
 */
add_task(async function test_catastrophic_error_excluded_site_vpn_on() {
  let content = await openCatastrophicErrorOnSite({
    isInclusion: false,
    isProtectionEnabled: true,
  });

  Assert.ok(content.tryAgainButtonEl, "Try again button should be present");
  Assert.ok(
    content.continueWithoutVpnButtonEl,
    "Continue without VPN button should be present"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the trying again state keeps the error's buttons, disabled.
 */
add_task(async function test_trying_again_disables_error_buttons() {
  for (let isDefaultMode of [false, true]) {
    let content = await openPanel({
      unauthenticated: false,
      error: ERRORS.GENERIC,
      isDefaultMode,
      isRetrying: true,
    });

    let statusBox = content.statusBoxEl;
    Assert.equal(
      statusBox.titleEl.getAttribute("data-l10n-id"),
      "ipprotection-connection-status-trying-again-title",
      "Title should say the VPN is trying again"
    );
    Assert.ok(!statusBox.descriptionEl, "Description should not be shown");
    await checkStatusBoxAriaLabel(statusBox);

    Assert.ok(
      content.tryAgainButtonEl.disabled,
      "Try again button should be disabled"
    );
    if (isDefaultMode) {
      Assert.ok(
        content.continueWithoutVpnButtonEl.disabled,
        "Continue without VPN button should be disabled"
      );
    } else {
      Assert.ok(
        !content.continueWithoutVpnButtonEl,
        "Continue without VPN button should not be present"
      );
    }

    await closePanel();
  }
  await setPanelState({ isRetrying: false });
});

/**
 * Tests that clicking Try again shows the trying again state until the
 * retry fails, then returns to the error.
 */
add_task(async function test_try_again_shows_trying_again_until_failure() {
  const sandbox = sinon.createSandbox();
  let { promise, resolve } = Promise.withResolvers();
  let startStub = sandbox.stub(IPPProxyManager, "start").returns(promise);

  let content = await openPanel({
    unauthenticated: false,
    error: ERRORS.GENERIC,
    isDefaultMode: true,
  });

  content.tryAgainButtonEl.click();
  await TestUtils.waitForCondition(
    () => startStub.called,
    "Retrying should try to start the proxy"
  );
  await content.updateComplete;

  Assert.equal(
    content.statusBoxEl.titleEl.getAttribute("data-l10n-id"),
    "ipprotection-connection-status-trying-again-title",
    "Title should say the VPN is trying again while retrying"
  );
  Assert.ok(
    content.tryAgainButtonEl.disabled,
    "Try again button should be disabled while retrying"
  );
  Assert.ok(
    content.continueWithoutVpnButtonEl.disabled,
    "Continue without VPN button should be disabled while retrying"
  );

  resolve({ error: ERRORS.GENERIC });
  await TestUtils.waitForCondition(
    () => !content.state.isRetrying,
    "Retry should finish"
  );
  await content.updateComplete;

  Assert.equal(
    content.statusBoxEl.titleEl.getAttribute("data-l10n-id"),
    "ipprotection-connection-status-blocked-error-title-1",
    "Title should return to the error after the retry fails"
  );
  Assert.ok(
    !content.tryAgainButtonEl.disabled,
    "Try again button should be enabled after the retry fails"
  );
  Assert.ok(
    !content.continueWithoutVpnButtonEl.disabled,
    "Continue without VPN button should be enabled after the retry fails"
  );

  await closePanel();
  sandbox.restore();
});
