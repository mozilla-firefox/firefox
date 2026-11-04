/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const MOCK_SITE_NAME = "https://example.com";

const INCLUSIONS_PREF = "browser.ipProtection.features.siteInclusions";

const MANAGE_RULES_L10N_ID = "site-rules-manage-rules-link-text";

const STATUS_HEADING_L10N_ID = "site-rules-status-heading";
const EXCLUSION_DESCRIPTION_L10N_ID = "site-rules-description-exclusion";
const INCLUSION_DESCRIPTION_L10N_ID = "site-rules-description-inclusion";

const VPN_ON_ICON =
  "chrome://browser/content/ipprotection/assets/states/ipprotection-on.svg";
const VPN_EXCLUDED_ICON =
  "chrome://browser/content/ipprotection/assets/states/ipprotection-excluded.svg";

const SETTINGS_PANE = "privacy-vpnsiterules";

/**
 * The feature pref is re-read from the pref on every panel open (see
 * IPProtectionPanel.updateComponentState), so it cannot be injected as panel
 * state - it has to be pushed as a real pref.
 *
 * @param {boolean} enabled
 *  Whether site inclusions should be enabled.
 */
async function pushInclusionsPref(enabled) {
  await SpecialPowers.pushPrefEnv({
    set: [[INCLUSIONS_PREF, enabled]],
  });
}

/**
 * @param {IPProtectionContentElement} content
 *  The panel content element.
 * @returns {Element|null}
 *  The site rule status card, if any.
 */
function getSiteRuleStatus(content) {
  return content.shadowRoot.querySelector("#site-rule-status-container");
}

/**
 * @param {IPProtectionContentElement} content
 *  The panel content element.
 * @returns {Element|null}
 *  The icon of the site rule status card, if any.
 */
function getSiteRuleStatusIcon(content) {
  return content.shadowRoot.querySelector("#site-rule-status-container img");
}

/**
 * openPanel() applies its state before showing(), which then recomputes
 * siteData from the loaded tab. Re-apply the state once the panel is up so the
 * injected siteData survives, for the cases that exercise the rendering rather
 * than the rule lookup.
 *
 * @param {object} state
 *  The panel state to apply.
 * @returns {Promise<IPProtectionContentElement>}
 *  The panel content element.
 */
async function openPanelWithSiteData(state) {
  let content = await openPanel(state);
  await setPanelState(state);

  return content;
}

/**
 * Asserts the status card shows the given rule description and icon, and that
 * both strings actually resolve, which guards against the Fluent messages
 * being renamed or removed.
 *
 * @param {IPProtectionContentElement} content
 *  The panel content element.
 * @param {string} descriptionL10nId
 *  The expected description string ID.
 * @param {string} iconSrc
 *  The expected icon URL.
 */
async function checkSiteRuleStatus(content, descriptionL10nId, iconSrc) {
  let statusEl = getSiteRuleStatus(content);
  Assert.ok(statusEl, "Site rule status card should be present");
  Assert.ok(
    BrowserTestUtils.isVisible(statusEl),
    "Site rule status card should be visible"
  );

  let headingEl = statusEl.querySelector("#site-rule-heading");
  Assert.ok(headingEl, "Site rule status card should have a heading");
  Assert.equal(
    headingEl.getAttribute("data-l10n-id"),
    STATUS_HEADING_L10N_ID,
    "Site rule status heading should use the status heading string"
  );
  Assert.ok(
    headingEl.classList.contains("text-deemphasized"),
    "Site rule status heading should be deemphasized"
  );

  let descriptionEl = statusEl.querySelector("#site-rule-description");
  Assert.ok(descriptionEl, "Site rule status card should have a description");
  Assert.equal(
    descriptionEl.getAttribute("data-l10n-id"),
    descriptionL10nId,
    `Site rule status description should use ${descriptionL10nId}`
  );

  Assert.equal(
    getSiteRuleStatusIcon(content)?.getAttribute("src"),
    iconSrc,
    `Site rule status card should show ${iconSrc}`
  );

  await TestUtils.waitForCondition(
    () => headingEl.textContent.trim() && descriptionEl.textContent.trim(),
    "Waiting for the site rule status card to be localized"
  );

  let manageButtonEl = content.siteRulesEl.buttonEl;
  await TestUtils.waitForCondition(
    () => manageButtonEl.ariaDescribedByElements?.length,
    "Waiting for the site rules link to be described by the status card"
  );
  let [describedByHeadingEl, describedByDescriptionEl] =
    manageButtonEl.ariaDescribedByElements;
  Assert.equal(
    describedByHeadingEl,
    headingEl,
    "Site rules link should be described by the status heading"
  );
  Assert.equal(
    describedByDescriptionEl,
    descriptionEl,
    "Site rules link should be described by the status description"
  );
}

/**
 * Tests that the site rules link is not shown when the feature pref is
 * disabled, and that the exclusion toggle is shown in its place.
 */
add_task(async function test_site_rules_feature_pref_disabled() {
  await pushInclusionsPref(false);

  setupService({
    isReady: true,
  });

  let content = await openPanel({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    !content.siteRulesEl,
    "Site rules link should not be present when the feature pref is disabled"
  );
  Assert.ok(
    content.siteExclusionControlEl,
    "Site exclusion control should still be present when inclusions are off"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that enabling site inclusions swaps the exclusion toggle for the
 * site rules link.
 */
add_task(async function test_site_rules_replaces_exclusion_toggle() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanel({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  Assert.ok(
    content.siteRulesEl,
    "Site rules link should be present when the feature pref is enabled"
  );
  Assert.ok(
    !content.siteExclusionControlEl,
    "Site exclusion control should not be present when inclusions are on"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that we don't show the site rules link if siteData is invalid.
 */
add_task(async function test_site_rules_requires_siteData() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanel({
    isProtectionEnabled: true,
  });

  // showing() recomputes siteData from the currently loaded tab on every open,
  // so it has to be cleared once the panel is up rather than via openPanel.
  await setPanelState({
    isProtectionEnabled: true,
    siteData: null,
  });

  Assert.ok(
    !content.siteRulesEl,
    "Site rules link should not be present without siteData"
  );

  let siteRulesVisiblePromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => content.siteRulesEl
  );

  await setPanelState({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  await Promise.all([content.updateComplete, siteRulesVisiblePromise]);

  Assert.ok(
    content.siteRulesEl,
    "Site rules link should appear once siteData is available"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that we don't show the site rules link when the panel is in an
 * error state.
 */
add_task(async function test_site_rules_hidden_on_error() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanel({
    isProtectionEnabled: true,
    error: "generic",
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  // showing() recomputes siteData from the currently loaded tab on every open,
  // so it has to be cleared once the panel is up rather than via openPanel.
  await setPanelState({
    isProtectionEnabled: true,
    error: "generic",
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  Assert.ok(
    !content.siteRulesEl,
    "Site rules link should not be present in an error state"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the site rules link is shown regardless of whether the VPN is
 * currently connected
 */
add_task(async function test_site_rules_visible_with_vpn_off() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanel({
    isProtectionEnabled: false,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  // showing() recomputes siteData from the currently loaded tab on every open,
  // so it has to be cleared once the panel is up rather than via openPanel.
  await setPanelState({
    isProtectionEnabled: false,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  Assert.ok(
    content.siteRulesEl,
    "Site rules link should be present with the VPN off"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests the content of the manage site rules section
 */
add_task(async function test_site_rules_manage_rules_content() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanel({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  // showing() recomputes siteData from the currently loaded tab on every open,
  // so it has to be cleared once the panel is up rather than via openPanel.
  await setPanelState({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  let siteRulesEl = content.siteRulesEl;
  Assert.ok(siteRulesEl, "Site rules control should be present");

  Assert.equal(
    siteRulesEl.localName,
    "moz-button",
    "Site rules control should be a moz-button"
  );
  Assert.equal(
    siteRulesEl.getAttribute("type"),
    "ghost",
    "Site rules control should be a ghost button"
  );
  Assert.equal(
    siteRulesEl.getAttribute("iconsrc"),
    "chrome://browser/content/ipprotection/assets/vpn-website-rules-icon.svg",
    "Site rules control should show the permissions icon"
  );
  Assert.equal(
    siteRulesEl.getAttribute("data-l10n-id"),
    MANAGE_RULES_L10N_ID,
    "Site rules control should use the manage rules string"
  );
  // Guards against the Fluent message being renamed or removed.
  await TestUtils.waitForCondition(
    () => siteRulesEl.textContent.trim(),
    "Waiting for the site rules control to be localized"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that activating the site rules link closes the panel and opens the
 * site rules settings sub-pane
 */
add_task(async function test_site_rules_link_opens_settings() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    MOCK_SITE_NAME
  );

  let content = await openPanel({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  // showing() recomputes siteData from the currently loaded tab on every open,
  // so it has to be cleared once the panel is up rather than via openPanel.
  await setPanelState({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
      hasSiteRule: false,
    },
  });

  Assert.ok(content.siteRulesEl, "Site rules link should be present");

  Services.fog.testResetFOG();
  let panelHiddenPromise = waitForPanelEvent(document, "popuphidden");
  const openPreferencesStub = sinon.stub(window, "openPreferences");

  content.siteRulesEl.click();

  await panelHiddenPromise;

  let panelView = PanelMultiView.getViewNode(
    document,
    IPProtectionWidget.PANEL_ID
  );
  Assert.ok(!BrowserTestUtils.isVisible(panelView), "Panel should be closed");

  Assert.ok(
    openPreferencesStub.calledWith(SETTINGS_PANE),
    "openPreferences should be called with the site rules pane"
  );
  Assert.equal(
    gBrowser.selectedTab,
    tab,
    "Clicking the site rules link should not have navigated the current tab"
  );

  let events = Glean.ipprotection.rulesPageOpened.testGetValue();
  Assert.equal(events.length, 1, "Should have recorded one rules page event");
  Assert.equal(
    events[0].extra.entrypoint,
    "panel",
    "Rules page event should record the panel entrypoint"
  );

  openPreferencesStub.restore();
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests the site rules status card shown for a site the user has excluded while the VPN
 * is on, where the rule turns the VPN off for that site.
 */
add_task(async function test_site_rule_status_exclusion() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: true,
    siteData: {
      isExclusion: true,
      isInclusion: false,
      hasSiteRule: true,
    },
  });

  await checkSiteRuleStatus(
    content,
    EXCLUSION_DESCRIPTION_L10N_ID,
    VPN_EXCLUDED_ICON
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests the site rules status card shown for a site the user has included while the VPN
 * is off, where the rule turns the VPN on for that site.
 */
add_task(async function test_site_rule_status_inclusion() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: false,
    isActive: true,
    siteData: {
      isExclusion: false,
      isInclusion: true,
      hasSiteRule: true,
    },
  });

  Assert.ok(
    content.hasSiteInclusion,
    "Content element should report a site inclusion"
  );

  await checkSiteRuleStatus(
    content,
    INCLUSION_DESCRIPTION_L10N_ID,
    VPN_ON_ICON
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests the structure of the status card: the heading and description are
 * grouped in the text container, and the icon sits alongside that container
 * rather than inside it.
 */
add_task(async function test_site_rule_status_structure() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: true,
    siteData: {
      isExclusion: true,
      isInclusion: false,
      hasSiteRule: true,
    },
  });

  let statusEl = getSiteRuleStatus(content);
  Assert.ok(statusEl, "Site rule status card should be present");

  let textContainerEl = statusEl.querySelector(
    "#site-rule-status-text-container"
  );
  Assert.ok(
    textContainerEl,
    "Site rule status card should have a text container"
  );
  Assert.equal(
    textContainerEl.parentElement,
    statusEl,
    "Text container should be a direct child of the status card"
  );

  Assert.deepEqual(
    Array.from(textContainerEl.children).map(child => child.id),
    ["site-rule-heading", "site-rule-description"],
    "Text container should hold the heading followed by the description"
  );

  Assert.equal(
    getSiteRuleStatusIcon(content).parentElement,
    statusEl,
    "Icon should be a direct child of the status card, not of the text container"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * The site rules status card describes a rule that is currently changing the VPN's
 * behaviour for this site. A rule that agrees with the global VPN state is a
 * no-op, so there is nothing to report.
 */
add_task(async function test_site_rule_status_hidden_when_rule_is_a_noop() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: true,
    siteData: {
      isExclusion: false,
      isInclusion: true,
      hasSiteRule: true,
    },
  });

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should be hidden for an inclusion while the VPN is already on"
  );

  await setPanelState({
    isProtectionEnabled: false,
    isActive: true,
    siteData: {
      isExclusion: true,
      isInclusion: false,
      hasSiteRule: true,
    },
  });

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should be hidden for an exclusion while the VPN is already off"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * A site can be excluded from the proxy without the user having set a rule for
 * it (eg. the VPN's own infrastructure origins). There is no rule of the
 * user's to report, so the status card stays hidden even though the VPN is
 * effectively off for the site.
 */
add_task(async function test_site_rule_status_requires_user_rule() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: true,
    siteData: {
      isExclusion: true,
      isInclusion: false,
      hasSiteRule: false,
    },
  });

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should be hidden when the user has no rule for the site"
  );
  Assert.ok(
    content.siteRulesEl,
    "Site rules link should still be present without a user rule"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the status card is gated on the feature pref, and that no part of
 * it leaks into the exclusion toggle layout.
 */
add_task(async function test_site_rule_status_feature_pref_disabled() {
  await pushInclusionsPref(false);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: true,
    siteData: {
      isExclusion: true,
      isInclusion: false,
      hasSiteRule: true,
    },
  });

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should not be present when the feature pref is disabled"
  );
  Assert.ok(
    content.siteExclusionControlEl,
    "Site exclusion control should be shown in its place"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that we don't show the status card when the panel is in an error
 * state, since the reported rule would not reflect what the VPN is doing.
 */
add_task(async function test_site_rule_status_hidden_on_error() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: true,
    error: "generic",
    siteData: {
      isExclusion: true,
      isInclusion: false,
      hasSiteRule: true,
    },
  });

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should not be present in an error state"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that we don't show the status card when the proxy is not active.
 */
add_task(async function test_site_rule_status_hidden_when_proxy_inactive() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: false,
    siteData: {
      isExclusion: true,
      isInclusion: false,
      hasSiteRule: true,
    },
  });

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should not be present when the proxy is not active"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the status card appears and disappears as the global VPN state
 * changes under a fixed exclusion rule.
 */
add_task(async function test_site_rule_status_follows_protection_state() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  const siteData = {
    isExclusion: true,
    isInclusion: false,
    hasSiteRule: true,
  };

  let content = await openPanelWithSiteData({
    isProtectionEnabled: false,
    isActive: true,
    siteData,
  });

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should start hidden with the VPN off"
  );

  let statusShownPromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => getSiteRuleStatus(content)
  );

  await setPanelState({
    isProtectionEnabled: true,
    isActive: true,
    siteData,
  });
  await Promise.all([content.updateComplete, statusShownPromise]);

  await checkSiteRuleStatus(
    content,
    EXCLUSION_DESCRIPTION_L10N_ID,
    VPN_EXCLUDED_ICON
  );

  let statusHiddenPromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => !getSiteRuleStatus(content)
  );

  await setPanelState({
    isProtectionEnabled: false,
    isActive: true,
    siteData,
  });
  await Promise.all([content.updateComplete, statusHiddenPromise]);

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should be removed once the rule no longer changes anything"
  );
  await TestUtils.waitForCondition(
    () => !content.siteRulesEl.buttonEl.ariaDescribedByElements,
    "Site rules link should no longer be described once the status card is removed"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the status card and the site rules link are shown together, with
 * the card above the link, and that the link offers to manage rules.
 */
add_task(async function test_site_rule_status_precedes_settings_link() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: true,
    siteData: {
      isExclusion: true,
      isInclusion: false,
      hasSiteRule: true,
    },
  });

  let statusEl = getSiteRuleStatus(content);
  let siteRulesEl = content.siteRulesEl;

  Assert.ok(statusEl, "Status card should be present");
  Assert.ok(siteRulesEl, "Site rules link should be present alongside it");
  Assert.ok(
    statusEl.compareDocumentPosition(siteRulesEl) &
      Node.DOCUMENT_POSITION_FOLLOWING,
    "Status card should come before the site rules link"
  );
  Assert.equal(
    siteRulesEl.getAttribute("data-l10n-id"),
    MANAGE_RULES_L10N_ID,
    "Site rules link should offer to manage rules when a rule is reported"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that siteData predating inclusions support (no isInclusion field) is
 * treated as having no inclusion rather than throwing.
 */
add_task(async function test_has_site_inclusion_defaults_to_false() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let content = await openPanelWithSiteData({
    isProtectionEnabled: true,
    isActive: true,
    siteData: {
      isExclusion: false,
      hasSiteRule: true,
    },
  });

  Assert.ok(
    !content.hasSiteInclusion,
    "hasSiteInclusion should be false when siteData omits isInclusion"
  );
  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should be hidden when neither rule type applies"
  );

  await closePanel();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the panel resolves a site's rule through the SiteRuleManager
 * instead of taking it from injected state, for an inclusion the user holds on
 * the loaded site.
 */
add_task(async function test_site_rule_status_inclusion_from_rule() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  // The tab goes first: an inclusion brings the proxy up, and the included
  // site would then load through it.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    MOCK_SITE_NAME
  );

  const principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      MOCK_SITE_NAME
    );
  IPPPermissionRules.setRule(principal, IPPPrincipalRules.INCLUDED);

  await waitForProxyState(IPPProxyStates.ACTIVE);
  let content = await openPanel();

  Assert.ok(
    content.state.siteData?.hasSiteRule,
    "The panel should report a rule for a site the user included"
  );
  Assert.ok(
    content.hasSiteInclusion,
    "The panel should resolve the rule as an inclusion"
  );

  await closePanel();
  BrowserTestUtils.removeTab(tab);
  // The rule goes before the proxy, or stopping it just brings it back.
  IPPPermissionRules.setRule(principal, IPPPrincipalRules.DEFAULT);
  await waitForProxyState(IPPProxyStates.READY);
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that a site the user holds no rule for reports no rule, so the status
 * card stays out of the panel.
 */
add_task(async function test_no_site_rule_status_without_a_rule() {
  await pushInclusionsPref(true);

  setupService({
    isReady: true,
  });

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    MOCK_SITE_NAME
  );

  let content = await openPanel({
    isProtectionEnabled: false,
  });

  Assert.ok(
    content.state.siteData,
    "The panel should report site data for a manageable site"
  );
  Assert.ok(
    !content.state.siteData.hasSiteRule,
    "The panel should report no rule for a site the user has not set"
  );
  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should be absent without a rule"
  );

  await closePanel();
  BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests that the status card follows the selected tab while the panel is open,
 * when the VPN only carries included sites.
 */
add_task(async function test_site_rule_status_updates_on_tab_switch() {
  await pushInclusionsPref(true);
  const sandbox = sinon.createSandbox();

  setupService({
    isReady: true,
  });

  sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.ACTIVE);
  sandbox.stub(IPPProxyManager, "mode").value(IPPMode.MODE_INCLUSION);

  const DEFAULT_SITE = "https://example.com";
  const INCLUDED_SITE = "https://example.org";

  Services.perms.addFromPrincipal(
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      INCLUDED_SITE
    ),
    "ipp-vpn",
    Ci.nsIPermissionManager.ALLOW_ACTION
  );

  let defaultTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    DEFAULT_SITE
  );
  let includedTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    INCLUDED_SITE
  );
  await BrowserTestUtils.switchTab(gBrowser, defaultTab);

  let content = await openPanel({
    isProtectionEnabled: false,
    isActive: true,
  });

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should be hidden for a site without a rule"
  );

  let statusShownPromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => getSiteRuleStatus(content)
  );
  await BrowserTestUtils.switchTab(gBrowser, includedTab);
  await Promise.all([content.updateComplete, statusShownPromise]);

  await checkSiteRuleStatus(
    content,
    INCLUSION_DESCRIPTION_L10N_ID,
    VPN_ON_ICON
  );

  let statusHiddenPromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => !getSiteRuleStatus(content)
  );
  await BrowserTestUtils.switchTab(gBrowser, defaultTab);
  await Promise.all([content.updateComplete, statusHiddenPromise]);

  Assert.ok(
    !getSiteRuleStatus(content),
    "Site rule status card should be hidden after switching back"
  );

  await closePanel();
  BrowserTestUtils.removeTab(includedTab);
  BrowserTestUtils.removeTab(defaultTab);
  sandbox.restore();
  Services.perms.removeByType("ipp-vpn");
  await SpecialPowers.popPrefEnv();
});
