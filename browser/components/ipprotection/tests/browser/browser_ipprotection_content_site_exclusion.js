/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { IPPExceptionsManager } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPPExceptionsManager.sys.mjs"
);

const MOCK_SITE_NAME = "https://example.com";

const PERM_NAME = "ipp-vpn";

const ENABLE_VPN_EVENT = "IPProtection:UserEnableVPNForSite";
const DISABLE_VPN_EVENT = "IPProtection:UserDisableVPNForSite";

/**
 * Tests that we don't show the exclusion toggle and ipprotection-excluded icon
 * when the feature pref is disabled.
 */
add_task(async function test_site_exclusion_feature_pref_disabled() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.ipProtection.features.siteExceptions", false]],
  });

  const sandbox = sinon.createSandbox();
  Services.perms.removeByType(PERM_NAME);

  sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.ACTIVE);

  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    MOCK_SITE_NAME
  );

  let content = await openPanel({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
    },
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    !content.siteExclusionControlEl,
    "Site exclusion control should not be present when feature pref is disabled"
  );

  let toolbarButton = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    toolbarButton.classList.contains("ipprotection-on"),
    "Toolbar icon should show connection status when feature pref is disabled"
  );
  Assert.ok(
    !toolbarButton.classList.contains("ipprotection-excluded"),
    "Toolbar icon should not show excluded status when feature pref is disabled"
  );

  await closePanel();
  BrowserTestUtils.removeTab(tab);
  Services.perms.removeByType(PERM_NAME);
  sandbox.restore();
  await SpecialPowers.popPrefEnv();
});

/**
 * Tests the site exclusion toggle visibility with VPN on or off.
 */
add_task(async function test_site_exclusion_toggle_with_siteData() {
  setupService({
    isReady: true,
  });
  await IPPFxaAuthProvider.checkForUpgrade();

  let content = await openPanel({
    isProtectionEnabled: false,
    siteData: {
      isExclusion: false,
    },
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );

  // VPN is off
  Assert.ok(
    !content.siteExclusionControlEl,
    "Site exclusion control should not be present with VPN off"
  );

  let siteExclusionVisiblePromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => content.siteExclusionControlEl
  );

  // Turn VPN on
  await setPanelState({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
    },
  });

  await Promise.all([content.updateComplete, siteExclusionVisiblePromise]);

  Assert.ok(
    content.siteExclusionControlEl,
    "Site exclusion control should be present with VPN on"
  );
  Assert.ok(
    content.siteExclusionToggleEl,
    "Site exclusion toggle should be present with VPN on"
  );

  await closePanel();
});

/**
 * Tests that we don't show the site exclusion toggle if siteData is invalid.
 */
add_task(async function test_site_exclusion_toggle_no_siteData() {
  setupService({
    isReady: true,
  });
  await IPPFxaAuthProvider.checkForUpgrade();

  let content = await openPanel({
    isProtectionEnabled: false,
    siteData: null,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    !content.siteExclusionControlEl,
    "Site exclusion control should not be present"
  );

  await closePanel();
});

/**
 * Tests that we don't show the site exclusion toggle when an error occurs.
 */
add_task(async function test_site_exclusion_VPN_error() {
  setupService({
    isReady: true,
  });
  await IPPFxaAuthProvider.checkForUpgrade();

  let content = await openPanel({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
    },
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );

  Assert.ok(
    content.siteExclusionControlEl,
    "Site exclusion control should be present with VPN on"
  );

  let siteExclusionHiddenPromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => !content.siteExclusionControlEl
  );

  // Turn VPN on
  await setPanelState({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
    },
    error: "generic-error",
  });

  await Promise.all([content.updateComplete, siteExclusionHiddenPromise]);

  Assert.ok(
    !content.siteExclusionControlEl,
    "Site exclusion control should be not present due to an error"
  );

  await closePanel();
});

/**
 * Tests the site exclusion toggle is pressed if isExclusion is false.
 */
add_task(async function test_site_exclusion_toggle_pressed_isExclusion() {
  setupService({
    isReady: true,
  });
  await IPPFxaAuthProvider.checkForUpgrade();

  let content = await openPanel({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
    },
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    content.siteExclusionControlEl,
    "Site exclusion control should be present with VPN on"
  );
  Assert.ok(
    content.siteExclusionToggleEl,
    "Site exclusion toggle should be present with VPN on"
  );
  Assert.ok(
    content.siteExclusionToggleEl.pressed,
    "Site exclusion toggle should be in pressed state when isExclusion is false"
  );

  let togglePressedPromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    {
      childList: true,
      subtree: true,
      attributes: true,
      attributeFilter: ["pressed"],
    },
    () => !content.siteExclusionToggleEl?.pressed
  );

  // Set isExclusion to true
  await setPanelState({
    isProtectionEnabled: true,
    siteData: {
      isExclusion: true,
    },
  });

  await Promise.all([content.updateComplete, togglePressedPromise]);

  Assert.ok(
    !content.siteExclusionToggleEl?.pressed,
    "Site exclusion toggle should not be in pressed state when isExclusion is true"
  );

  await closePanel();
});

/**
 * Tests the site exclusion toggle dispatches the expected events, calls
 * the appropriate IPPExceptionsManager methods, and correctly updates the toolbar button icon.
 */
add_task(
  async function test_site_exclusion_on_toggle_events_and_toolbar_icon() {
    const sandbox = sinon.createSandbox();
    Services.perms.removeByType(PERM_NAME);

    setupService({
      isReady: true,
    });
    await IPPFxaAuthProvider.checkForUpgrade();

    let setExclusionSpy = sandbox.spy(IPPExceptionsManager, "setExclusion");
    sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.ACTIVE);

    // Open a new foreground tab
    let tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      MOCK_SITE_NAME
    );

    let content = await openPanel({
      isProtectionEnabled: true,
      siteData: {
        isExclusion: false,
      },
    });

    let toolbarButton = document.getElementById(IPProtectionWidget.WIDGET_ID);

    Assert.ok(
      BrowserTestUtils.isVisible(content),
      "ipprotection content component should be present"
    );
    Assert.ok(
      content.siteExclusionControlEl,
      "Site exclusion control should be present with VPN on"
    );
    Assert.ok(
      content.siteExclusionToggleEl,
      "Site exclusion toggle should be present with VPN on"
    );
    Assert.ok(
      content.siteExclusionToggleEl.pressed,
      "Site exclusion toggle should be in pressed state (VPN enabled for site)"
    );

    // Click to disable VPN protection for site (add exclusion)
    let disableVPNEventPromise = BrowserTestUtils.waitForEvent(
      window,
      DISABLE_VPN_EVENT
    );
    content.siteExclusionToggleEl.click();
    await disableVPNEventPromise;

    Assert.ok(true, "Disable VPN protection for site event was dispatched");
    Assert.ok(
      setExclusionSpy.calledOnce,
      "IPPExceptionsManager.setExclusion should be called after disabling VPN"
    );
    Assert.strictEqual(
      setExclusionSpy.firstCall.args[1],
      true,
      "IPPExceptionsManager.setExclusion should be called with shouldExclude=true"
    );
    Assert.ok(
      toolbarButton.classList.contains("ipprotection-excluded"),
      "Toolbar icon should show the excluded status after disabling VPN for site"
    );

    // Click to enable VPN protection for site (remove exclusion)
    let enableVPNEventPromise = BrowserTestUtils.waitForEvent(
      window,
      ENABLE_VPN_EVENT
    );
    content.siteExclusionToggleEl.click();
    await enableVPNEventPromise;

    Assert.ok(true, "Enable VPN protection for site event was dispatched");
    Assert.ok(
      setExclusionSpy.calledTwice,
      "IPPExceptionsManager.setExclusion should be called two times now"
    );
    Assert.strictEqual(
      setExclusionSpy.secondCall.args[1],
      false,
      "IPPExceptionsManager.setExclusion should be called with shouldExclude=false"
    );
    Assert.ok(
      toolbarButton.classList.contains("ipprotection-on"),
      "Toolbar icon should show the connection status after enabling VPN for site"
    );

    // Clean up
    await closePanel();
    BrowserTestUtils.removeTab(tab);
    Services.perms.removeByType(PERM_NAME);
    sandbox.restore();
  }
);

/**
 * Tests that siteData and toggle pressed state update when navigating from
 * a non excluded site to an excluded site in a different tab.
 */
add_task(
  async function test_site_exclusion_updates_on_navigation_different_tab() {
    const sandbox = sinon.createSandbox();
    Services.perms.removeByType(PERM_NAME);

    setupService({
      isReady: true,
    });
    await IPPFxaAuthProvider.checkForUpgrade();

    sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.ACTIVE);

    const PROTECTED_SITE = "https://example.com";
    const EXCLUDED_SITE = "https://example.org";

    // Add second site as an exclusion
    const secondSitePrincipal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        EXCLUDED_SITE
      );
    Services.perms.addFromPrincipal(
      secondSitePrincipal,
      PERM_NAME,
      Ci.nsIPermissionManager.DENY_ACTION
    );

    // Open first tab (not excluded)
    let tab1 = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      PROTECTED_SITE
    );

    let content = await openPanel({
      isProtectionEnabled: true,
    });

    Assert.ok(
      BrowserTestUtils.isVisible(content),
      "ipprotection content component should be present"
    );
    Assert.ok(
      content.siteExclusionToggleEl,
      "Site exclusion toggle should be present"
    );
    Assert.ok(
      content.siteExclusionToggleEl.pressed,
      "Toggle should be in pressed state for first site (not excluded)"
    );

    let toolbarButton = document.getElementById(IPProtectionWidget.WIDGET_ID);
    Assert.ok(
      toolbarButton.classList.contains("ipprotection-on"),
      "Toolbar icon should show the connection status"
    );

    // Now open the second tab (site excluded)
    let tab2 = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      EXCLUDED_SITE
    );

    let siteDataUpdatePromise = BrowserTestUtils.waitForMutationCondition(
      content.shadowRoot,
      {
        childList: true,
        subtree: true,
        attributes: true,
      },
      () => content.state?.siteData
    );

    await Promise.all([content.updateComplete, siteDataUpdatePromise]);

    Assert.ok(
      !content.siteExclusionToggleEl.pressed,
      "Toggle should not be in pressed state for the second site (which is excluded)"
    );
    Assert.ok(
      toolbarButton.classList.contains("ipprotection-excluded"),
      "Toolbar icon should show the excluded status"
    );

    await closePanel();
    BrowserTestUtils.removeTab(tab2);
    BrowserTestUtils.removeTab(tab1);

    Services.perms.removeByType(PERM_NAME);
    sandbox.restore();
  }
);

/**
 * Tests that siteData and toggle pressed state update when navigating from
 * a non excluded site to an excluded site in the same tab.
 */
add_task(async function test_site_exclusion_updates_on_navigation_same_tab() {
  const sandbox = sinon.createSandbox();
  Services.perms.removeByType(PERM_NAME);

  setupService({
    isReady: true,
  });
  await IPPFxaAuthProvider.checkForUpgrade();

  sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.ACTIVE);

  const PROTECTED_SITE = "https://example.com";
  const EXCLUDED_SITE = "https://example.org";

  const secondSitePrincipal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      EXCLUDED_SITE
    );
  Services.perms.addFromPrincipal(
    secondSitePrincipal,
    PERM_NAME,
    Ci.nsIPermissionManager.DENY_ACTION
  );

  // Open the first site (not excluded) first
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROTECTED_SITE
  );

  let content = await openPanel({
    isProtectionEnabled: true,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    content.siteExclusionToggleEl,
    "Site exclusion toggle should be present"
  );
  Assert.ok(
    content.siteExclusionToggleEl.pressed,
    "Toggle should be in pressed state for first site (not excluded)"
  );

  let toolbarButton = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    toolbarButton.classList.contains("ipprotection-on"),
    "Toolbar icon should show the connection status"
  );

  let siteDataUpdatePromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    {
      childList: true,
      subtree: true,
      attributes: true,
    },
    () => content.state?.siteData
  );

  // Now load the second site (excluded)
  BrowserTestUtils.startLoadingURIString(tab.linkedBrowser, EXCLUDED_SITE);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser);

  await Promise.all([content.updateComplete, siteDataUpdatePromise]);

  Assert.ok(
    !content.siteExclusionToggleEl.pressed,
    "Toggle should not be in pressed state for the second site (which is excluded)"
  );
  Assert.ok(
    toolbarButton.classList.contains("ipprotection-excluded"),
    "Toolbar icon should show the excluded status"
  );

  await closePanel();
  BrowserTestUtils.removeTab(tab);

  Services.perms.removeByType(PERM_NAME);
  sandbox.restore();
});

/**
 * Tests that the toolbar icon updates when switching between tabs with different exclusion states.
 */
add_task(async function test_site_exclusion_updates_on_tab_switch() {
  const sandbox = sinon.createSandbox();
  Services.perms.removeByType(PERM_NAME);

  setupService({
    isReady: true,
  });
  await IPPFxaAuthProvider.checkForUpgrade();

  sandbox.stub(IPPProxyManager, "state").value(IPPProxyStates.ACTIVE);

  const PROTECTED_SITE = "https://example.com";
  const EXCLUDED_SITE = "https://example.org";

  // Add second site as an exclusion
  const excludedSitePrincipal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      EXCLUDED_SITE
    );
  Services.perms.addFromPrincipal(
    excludedSitePrincipal,
    PERM_NAME,
    Ci.nsIPermissionManager.DENY_ACTION
  );

  // Open first tab (not excluded)
  let protectedTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    PROTECTED_SITE
  );

  let toolbarButton = document.getElementById(IPProtectionWidget.WIDGET_ID);
  Assert.ok(
    toolbarButton.classList.contains("ipprotection-on"),
    "Toolbar icon should show connection status for opened protected site tab"
  );

  // Open second tab (excluded), making it the active tab
  let excludedTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    EXCLUDED_SITE
  );

  Assert.ok(
    toolbarButton.classList.contains("ipprotection-excluded"),
    "Toolbar icon should show excluded status for opened excluded site tab"
  );
  Assert.ok(
    !toolbarButton.classList.contains("ipprotection-on"),
    "Toolbar icon should not show connection status"
  );

  // Switch back to protected site tab
  await BrowserTestUtils.switchTab(gBrowser, protectedTab);

  Assert.ok(
    toolbarButton.classList.contains("ipprotection-on"),
    "Toolbar icon should show connection status after switching to protected site tab"
  );
  Assert.ok(
    !toolbarButton.classList.contains("ipprotection-excluded"),
    "Toolbar icon should not show excluded status after switching to protected site tab"
  );

  // Switch back to excluded site tab
  await BrowserTestUtils.switchTab(gBrowser, excludedTab);

  Assert.ok(
    toolbarButton.classList.contains("ipprotection-excluded"),
    "Toolbar icon should show excluded status after switching to excluded site tab"
  );
  Assert.ok(
    !toolbarButton.classList.contains("ipprotection-on"),
    "Toolbar icon should not show connection status after switching to excluded site tab"
  );

  // Cleanup
  BrowserTestUtils.removeTab(protectedTab);
  BrowserTestUtils.removeTab(excludedTab);
  Services.perms.removeByType(PERM_NAME);
  sandbox.restore();
});

/**
 * Tests that the site exclusion description is shown when the site is not
 * excluded, and hidden when the site is excluded.
 */
add_task(async function test_site_exclusion_description_visibility() {
  setupService({
    isSignedIn: true,
    isEnrolledAndEntitled: true,
  });

  let content = await openPanel({
    isSignedOut: false,
    isProtectionEnabled: true,
    siteData: {
      isExclusion: false,
    },
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    content.siteExclusionDescriptionEl,
    "Site exclusion description should be present when site is not excluded"
  );

  let descriptionHiddenPromise = BrowserTestUtils.waitForMutationCondition(
    content.shadowRoot,
    { childList: true, subtree: true },
    () => !content.siteExclusionDescriptionEl
  );

  await setPanelState({
    isSignedOut: false,
    isProtectionEnabled: true,
    siteData: {
      isExclusion: true,
    },
  });

  await Promise.all([content.updateComplete, descriptionHiddenPromise]);

  Assert.ok(
    !content.siteExclusionDescriptionEl,
    "Site exclusion description should not be present when site is excluded"
  );

  await closePanel();
});

/**
 * Tests that we don't show the site exclusion toggle on privileged pages.
 */
add_task(async function test_site_exclusion_toggle_privileged_page() {
  const sandbox = sinon.createSandbox();
  const ABOUT_PAGE = "about:about";

  setupService({
    isReady: true,
  });
  await IPPFxaAuthProvider.checkForUpgrade();

  let panel = IPProtection.getPanel(window);
  sandbox.stub(panel, "_isPrivilegedPage").returns(true);

  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, ABOUT_PAGE);

  let content = await openPanel({
    isProtectionEnabled: true,
  });

  Assert.ok(
    BrowserTestUtils.isVisible(content),
    "ipprotection content component should be present"
  );
  Assert.ok(
    !content.siteExclusionControlEl,
    "Site exclusion control should not be present on privileged pages"
  );

  await closePanel();
  BrowserTestUtils.removeTab(tab);
  sandbox.restore();
});
