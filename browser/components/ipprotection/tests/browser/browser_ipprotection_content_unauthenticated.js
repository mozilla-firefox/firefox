/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const lazy = {};

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const { BANDWIDTH, LINKS } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

const MAX_IN_GB_PREF = "browser.ipProtection.bandwidth.maxInGb";

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionWidget:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPProtectionPanel:
    "moz-src:///browser/components/ipprotection/IPProtectionPanel.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPProtectionStates:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
});

/**
 * Tests that the ip protection unauthenticated panel subview has the correct content.
 */
add_task(async function test_unauthenticated_content() {
  Assert.equal(
    lazy.IPProtectionService.state,
    lazy.IPProtectionStates.UNAUTHENTICATED,
    "Should be in the UNAUTHENTICATED state"
  );
  let button = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);

  let panelView = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionWidget.PANEL_ID
  );

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  // Open the panel
  button.click();
  await panelShownPromise;

  let content = panelView.querySelector(lazy.IPProtectionPanel.CONTENT_TAGNAME);

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );

  let unauthenticatedContent = content.unauthenticatedEl;

  Assert.ok(
    unauthenticatedContent,
    "Unauthenticated content should be visible"
  );

  let unauthenticatedImg = unauthenticatedContent.shadowRoot.querySelector(
    "#unauthenticated-vpn-img"
  );
  let unauthenticatedMessage = unauthenticatedContent.shadowRoot.querySelector(
    "#unauthenticated-vpn-message"
  );
  let getStartedButton = unauthenticatedContent.shadowRoot.querySelector(
    "#unauthenticated-get-started"
  );

  Assert.ok(unauthenticatedImg, "Unauthenticated image should be visible");
  Assert.ok(
    unauthenticatedMessage,
    "Unauthenticated message should be visible"
  );
  Assert.ok(getStartedButton, "Unauthenticated button should be visible");

  // Close the panel
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHiddenPromise;
});

/**
 * Tests get started button functionality.
 */
add_task(async function test_signin_button() {
  setupService({
    isReady: false,
  });
  Assert.equal(
    lazy.IPProtectionService.state,
    lazy.IPProtectionStates.UNAUTHENTICATED,
    "Should be in the UNAUTHENTICATED state"
  );

  let button = document.getElementById(lazy.IPProtectionWidget.WIDGET_ID);

  let panelView = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionWidget.PANEL_ID
  );

  let panelShownPromise = waitForPanelEvent(document, "popupshown");
  // Open the panel
  button.click();
  await panelShownPromise;

  let content = panelView.querySelector(lazy.IPProtectionPanel.CONTENT_TAGNAME);
  let unauthenticatedContent = content.unauthenticatedEl;

  Assert.ok(
    unauthenticatedContent,
    "Unauthenticated content should be visible"
  );

  let getStartedButton = unauthenticatedContent.shadowRoot.querySelector(
    "#unauthenticated-get-started"
  );

  Assert.ok(getStartedButton, "Unauthenticated button should be visible");

  let optInPromise = BrowserTestUtils.waitForEvent(
    document,
    "IPProtection:OptIn"
  );
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  getStartedButton.click();
  await Promise.all([optInPromise, panelHiddenPromise]);

  let panelShownAgainPromise = waitForPanelEvent(document, "popupshown");
  await lazy.IPProtection.getPanel(window).enroll();
  await panelShownAgainPromise;

  // Close the panel
  let panelHiddenPromiseEnd = waitForPanelEvent(document, "popuphidden");

  panelView.dispatchEvent(
    new CustomEvent("IPProtection:Close", { bubbles: true })
  );

  await panelHiddenPromiseEnd;
  cleanupService();
});

/**
 * Tests that clicking "get started" in the panel passes vpn_integration_panel
 * as the entrypoint to fxaSignInFlow.
 */
add_task(async function test_panel_get_started_entrypoint() {
  setupService({
    isReady: false,
  });
  const { fxaSignInFlow } = STUBS;
  fxaSignInFlow.resetHistory();
  let content = await openPanel({ unauthenticated: true });
  let unauthenticatedContent = content.unauthenticatedEl;
  let getStartedButton = unauthenticatedContent.shadowRoot.querySelector(
    "#unauthenticated-get-started"
  );

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  let panelShownAgainPromise = waitForPanelEvent(document, "popupshown");
  getStartedButton.click();
  await panelHiddenPromise;
  await panelShownAgainPromise;

  Assert.ok(fxaSignInFlow.calledOnce, "fxaSignInFlow should be called once");
  Assert.equal(
    fxaSignInFlow.firstCall.args[0].entrypoint,
    "vpn_integration_panel",
    "entrypoint should be vpn_integration_panel when enrolling from the panel"
  );
  Assert.equal(
    fxaSignInFlow.firstCall.args[0].extraParams.utm_source,
    "panel",
    "utm_source should be panel when enrolling from the panel"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that clicking the "learn-more-vpn" link opens the support URL in a new tab
 * and closes the panel.
 */
add_task(async function test_learn_more_vpn_link() {
  setupService({
    isReady: false,
  });

  let content = await openPanel({ unauthenticated: true });
  let unauthenticatedContent = content.unauthenticatedEl;

  Assert.ok(
    unauthenticatedContent,
    "Unauthenticated content should be visible"
  );

  let learnMoreLink =
    unauthenticatedContent.shadowRoot.querySelector(".learn-more-vpn");

  Assert.ok(learnMoreLink, "Learn more VPN link should be present");

  let openWebLinkInStub = sinon.stub(window, "openWebLinkIn");

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  learnMoreLink.click();
  await panelHiddenPromise;

  Assert.ok(
    openWebLinkInStub.calledOnce,
    "openWebLinkIn should be called once"
  );

  const expectedUrl =
    Services.urlFormatter.formatURLPref("app.support.baseURL") +
    LINKS.SUPPORT_SLUG;
  Assert.equal(
    openWebLinkInStub.firstCall.args[0],
    expectedUrl,
    "openWebLinkIn should be called with the support URL"
  );
  Assert.equal(
    openWebLinkInStub.firstCall.args[1],
    "tab",
    "openWebLinkIn should open in a tab"
  );

  openWebLinkInStub.restore();
  cleanupService();
});

/**
 * Tests that clicking the terms of service link opens the correct URL in a new
 * tab and closes the panel.
 */
add_task(async function test_terms_of_service_link() {
  setupService({
    isSignedIn: false,
    isEnrolledAndEntitled: false,
  });

  let content = await openPanel({ unauthenticated: true });
  let unauthenticatedContent = content.unauthenticatedEl;

  let tosLink = unauthenticatedContent.shadowRoot.querySelector(
    "#vpn-terms-of-service"
  );

  Assert.ok(tosLink, "Terms of service link should be present");

  let openWebLinkInStub = sinon.stub(window, "openWebLinkIn");

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  tosLink.click();
  await panelHiddenPromise;

  Assert.ok(
    openWebLinkInStub.calledOnce,
    "openWebLinkIn should be called once"
  );
  Assert.equal(
    openWebLinkInStub.firstCall.args[0],
    LINKS.TERMS_OF_SERVICE_URL,
    "openWebLinkIn should be called with the terms of service URL"
  );
  Assert.equal(
    openWebLinkInStub.firstCall.args[1],
    "tab",
    "openWebLinkIn should open in a tab"
  );

  openWebLinkInStub.restore();
  cleanupService();
});

/**
 * Tests that clicking the privacy notice link opens the correct URL in a new
 * tab and closes the panel.
 */
add_task(async function test_privacy_notice_link() {
  setupService({
    isSignedIn: false,
    isEnrolledAndEntitled: false,
  });

  let content = await openPanel({ unauthenticated: true });
  let unauthenticatedContent = content.unauthenticatedEl;

  let privacyLink = unauthenticatedContent.shadowRoot.querySelector(
    "#vpn-privacy-notice"
  );

  Assert.ok(privacyLink, "Privacy notice link should be present");

  let openWebLinkInStub = sinon.stub(window, "openWebLinkIn");

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  privacyLink.click();
  await panelHiddenPromise;

  Assert.ok(
    openWebLinkInStub.calledOnce,
    "openWebLinkIn should be called once"
  );
  Assert.equal(
    openWebLinkInStub.firstCall.args[0],
    LINKS.PRIVACY_NOTICE_URL,
    "openWebLinkIn should be called with the privacy notice URL"
  );
  Assert.equal(
    openWebLinkInStub.firstCall.args[1],
    "tab",
    "openWebLinkIn should open in a tab"
  );

  openWebLinkInStub.restore();
  cleanupService();
});

/**
 * Tests that BANDWIDTH.MAX_IN_GB use the bandwidth.maxInGb pref
 * and that the unauthenticated panel reflects the configured value.
 */
add_task(async function test_max_in_gb_pref() {
  Assert.equal(
    BANDWIDTH.MAX_IN_GB,
    50,
    "MAX_IN_GB should default to 50 when the pref is unset"
  );

  await SpecialPowers.pushPrefEnv({ set: [[MAX_IN_GB_PREF, 100]] });
  Assert.equal(
    BANDWIDTH.MAX_IN_GB,
    100,
    "MAX_IN_GB should reflect the pref value"
  );

  setupService({
    isSignedIn: false,
    isEnrolledAndEntitled: false,
  });
  let content = await openPanel({ unauthenticated: true });
  let bandwidthMessage = content.unauthenticatedEl.shadowRoot.querySelector(
    '[data-l10n-id="unauthenticated-bandwidth-limit-message"]'
  );

  Assert.ok(bandwidthMessage, "Bandwidth limit message should be present");
  Assert.deepEqual(
    JSON.parse(bandwidthMessage.getAttribute("data-l10n-args")),
    { maxUsage: 100 },
    "Panel should render with the pref-driven max usage"
  );

  await closePanel();
  cleanupService();
  await SpecialPowers.popPrefEnv();

  Assert.equal(
    BANDWIDTH.MAX_IN_GB,
    50,
    "MAX_IN_GB should return to the default after the pref is cleared"
  );
});

/**
 * Tests that clicking "get started" still calls fxaSignInFlow when signed in.
 */
add_task(async function test_panel_get_started_signed_in() {
  setupService({
    isReady: false,
  });
  STUBS.fxaSignInFlow.resetHistory();
  let content = await openPanel({ unauthenticated: true });
  let unauthenticatedContent = content.unauthenticatedEl;
  let getStartedButton = unauthenticatedContent.shadowRoot.querySelector(
    "#unauthenticated-get-started"
  );

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  let panelShownAgainPromise = waitForPanelEvent(document, "popupshown");
  getStartedButton.click();
  await panelHiddenPromise;
  await panelShownAgainPromise;

  Assert.ok(
    STUBS.fxaSignInFlow.calledOnce,
    "fxaSignInFlow should be called once"
  );

  await closePanel();
  cleanupService();
});
