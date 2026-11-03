/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { LINKS } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);
const lazy = {};

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

ChromeUtils.defineESModuleGetters(lazy, {
  IPProtectionWidget:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPProtectionPanel:
    "moz-src:///browser/components/ipprotection/IPProtectionPanel.sys.mjs",
  IPProtectionService:
    "moz-src:///toolkit/components/ipprotection/IPProtectionService.sys.mjs",
  IPPDummyAuthProvider:
    "resource://testing-common/ipprotection/IPPDummyAuthProvider.sys.mjs",
  IPPNimbusHelper:
    "moz-src:///toolkit/components/ipprotection/IPPNimbusHelper.sys.mjs",
});

const PANELSTATES = {
  signedOutVPNOff: {
    unauthenticated: true,
    isProtectionEnabled: false,
  },
  signedInVPNOff: {
    unauthenticated: false,
    isProtectionEnabled: false,
  },
  signedInVPNOn: {
    unauthenticated: false,
    isProtectionEnabled: true,
  },
};

async function setAuthenticated(content, isReady, sandbox) {
  if (isReady) {
    lazy.IPPDummyAuthProvider.simulateSignIn(true);
    lazy.IPPDummyAuthProvider.setEntitlement(createTestEntitlement(), {
      silent: true,
    });
  } else {
    lazy.IPPDummyAuthProvider.simulateSignIn(false);
  }
  sandbox.stub(lazy.IPPNimbusHelper, "hidesFeature").returns(false);
  lazy.IPProtectionService.updateState();
  content.requestUpdate();
  await content.updateComplete;
}

async function resetStateToObj(content, originalState) {
  content.state = originalState;
  content.requestUpdate();
  await content.updateComplete;
}

/**
 * Tests that the ip protection main panel view has the correct content.
 */
add_task(async function test_main_content() {
  let sandbox = sinon.createSandbox();

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

  let originalState = structuredClone(content.state);

  await setAuthenticated(content, true, sandbox);

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(content.statusCardEl, "Status card should be present");

  await resetStateToObj(content, originalState);

  // Close the panel
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  EventUtils.synthesizeKey("KEY_Escape");
  await panelHiddenPromise;

  sandbox.restore();
});

/**
 * Tests settings link visibility in different panel states.
 */
add_task(async function test_settings_link_visibility() {
  let content = await openPanel(PANELSTATES.signedOutVPNOff);

  Assert.ok(
    !content.settingsButtonEl,
    "Settings button should NOT be present when not signed in"
  );

  await closePanel();

  content = await openPanel(PANELSTATES.signedInVPNOff);

  Assert.ok(
    content.settingsButtonEl,
    "Settings button should be present when VPN is disabled"
  );

  await closePanel();

  content = await openPanel(PANELSTATES.signedInVPNOn);

  Assert.ok(
    content.settingsButtonEl,
    "Settings button should be present when VPN is enabled"
  );

  await closePanel();
});

/**
 * Tests that clicking the settings button closes the panel and calls
 * openPreferences with the correct argument.
 */
add_task(async function test_settings_button_closes_panel() {
  let content = await openPanel(PANELSTATES.signedInVPNOn);

  Assert.ok(BrowserTestUtils.isVisible(content), "VPN panel should be present");

  Assert.ok(content.settingsButtonEl, "Settings button should be present");

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");

  const openPreferencesStub = sinon.stub(window, "openPreferences");

  content.settingsButtonEl.click();

  await panelHiddenPromise;

  let panelView = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionWidget.PANEL_ID
  );
  Assert.ok(!BrowserTestUtils.isVisible(panelView), "Panel should be closed");

  Assert.ok(
    openPreferencesStub.calledWith("privacy-vpn"),
    "openPreferences called with correct argument when settings button clicked"
  );
  openPreferencesStub.restore();
});

/**
 * Tests settings link visibility in different panel states.
 */
add_task(async function test_settings_link_visibility() {
  let content = await openPanel(PANELSTATES.signedOutVPNOff);

  Assert.ok(
    !content.settingsButtonEl,
    "Settings button should NOT be present when not signed in"
  );

  await closePanel();

  content = await openPanel(PANELSTATES.signedInVPNOff);

  Assert.ok(
    content.settingsButtonEl,
    "Settings button should be present when VPN is disabled"
  );

  await closePanel();

  content = await openPanel(PANELSTATES.signedInVPNOn);

  Assert.ok(
    content.settingsButtonEl,
    "Settings button should be present when VPN is enabled"
  );

  await closePanel();
});

/**
 * Tests that clicking the settings button closes the panel and calls
 * openPreferences with the correct argument.
 */
add_task(async function test_settings_button_closes_panel() {
  let content = await openPanel(PANELSTATES.signedInVPNOn);

  Assert.ok(BrowserTestUtils.isVisible(content), "VPN panel should be present");

  Assert.ok(content.settingsButtonEl, "Settings button should be present");

  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");

  const openPreferencesStub = sinon.stub(window, "openPreferences");

  content.settingsButtonEl.click();

  await panelHiddenPromise;

  let panelView = PanelMultiView.getViewNode(
    document,
    lazy.IPProtectionWidget.PANEL_ID
  );
  Assert.ok(!BrowserTestUtils.isVisible(panelView), "Panel should be closed");

  Assert.ok(
    openPreferencesStub.calledWith("privacy-vpn"),
    "openPreferences called with correct argument when settings button clicked"
  );
  openPreferencesStub.restore();
});

/**
 * Tests the enrolling skeleton state renders in ipprotection-content.
 */
add_task(async function test_enrolling_skeleton() {
  let content = await openPanel({
    unauthenticated: false,
    isEnrolling: true,
  });

  let container = content.shadowRoot.querySelector("#enrolling-container");
  Assert.ok(container, "Enrolling container should be present");
  Assert.ok(
    container.querySelector(".skeleton-title"),
    "Skeleton title element should be present"
  );
  Assert.ok(
    container.querySelector(".skeleton-line"),
    "Skeleton line element should be present"
  );
  Assert.equal(
    container.querySelectorAll(".skeleton-line-thick").length,
    2,
    "Two skeleton line thick elements should be present"
  );
  Assert.ok(
    container.querySelector(".skeleton-image"),
    "Skeleton image element should be present"
  );

  Assert.ok(
    !content.statusCardEl,
    "Status card should be hidden while enrolling"
  );
  Assert.ok(
    !content.statusBoxEl,
    "Status box should be hidden while enrolling"
  );
  Assert.ok(
    content.settingsButtonEl,
    "Settings button should be present while enrolling"
  );

  await closePanel();
});

/**
 * Tests that the enrolling state takes priority over the unauthenticated state.
 */
add_task(async function test_enrolling_overrides_unauthenticated() {
  let content = await openPanel({
    unauthenticated: true,
    isEnrolling: true,
  });

  Assert.ok(
    !content.unauthenticatedEl,
    "Unauthenticated view should be hidden while enrolling"
  );
  Assert.ok(
    content.shadowRoot.querySelector("#enrolling-container"),
    "Enrolling skeleton should be shown instead"
  );

  await closePanel();
});

/**
 * Tests that the panel transitions from the enrolling skeleton to the normal
 * state once enrollment completes.
 */
add_task(async function test_enrolling_transitions_to_ready() {
  setupService({
    isReady: true,
    canEnroll: true,
    proxyPass: {
      status: 200,
      error: undefined,
      pass: makePass(),
    },
  });

  let content = await openPanel({
    unauthenticated: false,
    isProtectionEnabled: false,
    isEnrolling: true,
  });

  Assert.ok(
    content.shadowRoot.querySelector("#enrolling-container"),
    "Skeleton shown initially"
  );

  await setPanelState({
    unauthenticated: false,
    isProtectionEnabled: false,
    isEnrolling: false,
  });

  Assert.ok(
    !content.shadowRoot.querySelector("#enrolling-container"),
    "Skeleton hidden after enrollment completes"
  );
  Assert.ok(
    content.statusCardEl,
    "Status card shown after enrollment completes"
  );

  await closePanel();
  cleanupService();
});

/**
 * Tests that the site inclusion prompt is shown once when the user turns on
 * the VPN, follows navigation to the current website, and is removed when
 * dismissed.
 */
add_task(async function test_site_inclusion_prompt() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.ipProtection.features.siteInclusions", true],
      ["browser.ipProtection.siteInclusionPromptShown", false],
      ["browser.ipProtection.userEnabled", false],
      ["browser.ipProtection.userEnableCount", 0],
    ],
  });
  const EXCLUDED_SITE = "https://example.net";
  let sandbox = sinon.createSandbox();
  sandbox.stub(IPPProxyManager, "start").resolves({ started: true });
  sandbox
    .stub(IPPSiteRuleManager, "getRule")
    .callsFake(principal =>
      principal?.origin === EXCLUDED_SITE
        ? IPPPrincipalRules.EXCLUDED
        : IPPPrincipalRules.DEFAULT
    );

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://www.example.com"
  );
  let content = await openPanel({ isProtectionEnabled: false, error: "" });

  content.dispatchEvent(
    new CustomEvent("IPProtection:UserEnable", { bubbles: true })
  );
  await setPanelState({ isProtectionEnabled: true });

  let prompt = content.siteInclusionPromptEl;
  Assert.ok(prompt, "Prompt should be shown after the VPN is turned on");
  Assert.equal(prompt.localName, "moz-promo", "Prompt should be a moz-promo");
  Assert.ok(prompt.dismissable, "Prompt should be dismissable");
  Assert.deepEqual(
    JSON.parse(prompt.getAttribute("data-l10n-args")),
    { website: "example.com" },
    "Prompt should be passed the current website without www."
  );

  let loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://example.org"
  );
  await loaded;
  await content.updateComplete;

  prompt = content.siteInclusionPromptEl;
  Assert.deepEqual(
    JSON.parse(prompt.getAttribute("data-l10n-args")),
    { website: "example.org" },
    "Prompt should follow navigation to a new website"
  );
  Assert.ok(
    Services.prefs.getBoolPref("browser.ipProtection.siteInclusionPromptShown"),
    "Prompt should be recorded as shown"
  );

  loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, EXCLUDED_SITE);
  await loaded;
  await content.updateComplete;

  Assert.ok(
    !content.siteInclusionPromptEl,
    "Prompt should not be shown on an excluded website"
  );

  loaded = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  BrowserTestUtils.startLoadingURIString(
    tab.linkedBrowser,
    "https://example.org"
  );
  await loaded;
  await content.updateComplete;

  prompt = content.siteInclusionPromptEl;
  Assert.ok(prompt, "Prompt should return after leaving the excluded website");

  await prompt.updateComplete;
  let dismissed = BrowserTestUtils.waitForEvent(
    document,
    "IPProtection:DismissSiteInclusionPrompt"
  );
  prompt.closeButton.click();
  await dismissed;
  await content.updateComplete;

  Assert.ok(
    !content.siteInclusionPromptEl,
    "Prompt should be removed after it is dismissed"
  );

  content.dispatchEvent(
    new CustomEvent("IPProtection:UserEnable", { bubbles: true })
  );
  await content.updateComplete;

  Assert.ok(
    !content.siteInclusionPromptEl,
    "Prompt should not be shown a second time"
  );

  await closePanel();
  BrowserTestUtils.removeTab(tab);
  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});
