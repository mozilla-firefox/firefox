/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

// This file tests the Privacy pane's Firefox VPN UI.

"use strict";

const lazy = {};

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

ChromeUtils.defineESModuleGetters(lazy, {
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
  IPProtection:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
  IPProtectionWidget:
    "moz-src:///browser/components/ipprotection/IPProtection.sys.mjs",
});

const { IPProtectionActivator } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ipprotection/IPProtectionActivator.sys.mjs"
);

const { IPPDummyAuthProvider } = ChromeUtils.importESModule(
  "resource://testing-common/ipprotection/IPPDummyAuthProvider.sys.mjs"
);
IPProtectionActivator.setAuthProvider(IPPDummyAuthProvider);

const { BANDWIDTH } = ChromeUtils.importESModule(
  "chrome://browser/content/ipprotection/ipprotection-constants.mjs"
);

const FEATURE_PREF = "browser.ipProtection.enabled";
const SITE_EXCEPTIONS_FEATURE_PREF =
  "browser.ipProtection.features.siteExceptions";
const SITE_INCLUSIONS_FEATURE_PREF =
  "browser.ipProtection.features.siteInclusions";
const AUTOSTART_FEATURE_ENABLED_PREF =
  "browser.ipProtection.features.autoStart";
const BANDWIDTH_FEATURE_ENABLED_PREF = "browser.ipProtection.bandwidth.enabled";
const AUTOSTART_PREF = "browser.ipProtection.autoStartEnabled";
const AUTOSTART_PRIVATE_PREF = "browser.ipProtection.autoStartPrivateEnabled";
const ONBOARDING_MESSAGE_MASK_PREF =
  "browser.ipProtection.onboardingMessageMask";
const ENTITLEMENT_CACHE_PREF = "browser.ipProtection.entitlementCache";
const USAGE_CACHE_PREF = "browser.ipProtection.usageCache";
const IPP_ADDED_PREF = "browser.ipProtection.added";
const IPP_STATE_CACHE_PREF = "browser.ipProtection.stateCache";
const IPP_PANEL_HAS_OPENED_PREF = "browser.ipProtection.everOpenedPanel";
const IPP_CACHE_DISABLED_PREF = "browser.ipProtection.cacheDisabled";
const maxBytes = BANDWIDTH.MAX_IN_GB * BANDWIDTH.BYTES_IN_GB;
const UPGRADE_NOT_AVAILABLE_PREF = "browser.ipProtection.upgradeNotAvailable";

add_setup(async function ippSetup() {
  await SpecialPowers.pushPrefEnv({
    set: [[IPP_CACHE_DISABLED_PREF, true]],
  });

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref(ONBOARDING_MESSAGE_MASK_PREF);
    Services.prefs.clearUserPref(IPP_ADDED_PREF);
    Services.prefs.clearUserPref(IPP_STATE_CACHE_PREF);
    Services.prefs.clearUserPref(IPP_PANEL_HAS_OPENED_PREF);
  });
});

async function setupVpnPrefs({
  feature = false,
  siteExceptions = false,
  siteInclusions = false,
  autostartFeatureEnabled = false,
  bandwidth = false,
  autostart = false,
  autostartprivate = false,
  entitlementCache = "",
  usageCache = "",
  upgradeNotAvailable = false,
}) {
  let prefs = [
    [FEATURE_PREF, feature],
    [SITE_EXCEPTIONS_FEATURE_PREF, siteExceptions],
    [SITE_INCLUSIONS_FEATURE_PREF, siteInclusions],
    [AUTOSTART_FEATURE_ENABLED_PREF, autostartFeatureEnabled],
    [BANDWIDTH_FEATURE_ENABLED_PREF, bandwidth],
    [AUTOSTART_PREF, autostart],
    [AUTOSTART_PRIVATE_PREF, autostartprivate],
    [ENTITLEMENT_CACHE_PREF, entitlementCache],
    [USAGE_CACHE_PREF, usageCache],
    [UPGRADE_NOT_AVAILABLE_PREF, upgradeNotAvailable],
  ];

  return SpecialPowers.pushPrefEnv({
    set: prefs,
  });
}

function testSettingsGroupVisible(browser) {
  let settingGroup = browser.contentDocument.querySelector(
    `setting-group[groupid="ipprotection"]`
  );
  is_element_visible(settingGroup, "ipprotection setting group is shown");

  return settingGroup;
}

// Test the section is hidden on page load if the feature pref is disabled.
add_task(
  async function test_section_removed_when_set_to_ineligible_experiment_pref() {
    await setupVpnPrefs({ feature: false });

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        let settingGroup = browser.contentDocument.querySelector(
          `setting-group[groupid="ipprotection"]`
        );
        is_element_hidden(settingGroup, "ipprotection setting group is hidden");
      }
    );

    await SpecialPowers.popPrefEnv();
  }
);

// Test the section is shown on page load if the feature pref is enabled
add_task(
  async function test_section_shown_when_set_to_eligible_experiment_pref() {
    await setupVpnPrefs({ feature: true });

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        testSettingsGroupVisible(browser);
      }
    );
  }
);

// Test the site exceptions controls load correctly.
add_task(async function test_exceptions_settings() {
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    entitlementCache: '{"some":"data"}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let siteExceptionsGroup = settingGroup?.querySelector(
        "#ipProtectionExceptions"
      );
      is_element_visible(siteExceptionsGroup, "Site exceptions group is shown");

      let exceptionAllListButton = siteExceptionsGroup?.querySelector(
        "#ipProtectionExceptionAllListButton"
      );
      is_element_visible(
        exceptionAllListButton,
        "Button for list of exclusions is shown"
      );
    }
  );
});

// Test that we show the "Add" button in the site exceptions permission dialog
// and correctly add site exclusions.
add_task(async function test_exclusions_add_button() {
  const PERM_NAME = "ipp-vpn";
  await setupVpnPrefs({
    feature: "beta",
    siteExceptions: true,
    entitlementCache: '{"some":"data"}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let siteExceptionsGroup = settingGroup?.querySelector(
        "#ipProtectionExceptions"
      );
      let exceptionAllListButton = siteExceptionsGroup?.querySelector(
        "#ipProtectionExceptionAllListButton"
      );
      is_element_visible(
        exceptionAllListButton,
        "Button for list of exclusions is shown"
      );

      // Clear ipp-vpn to start with 0 exclusions
      Services.perms.removeByType(PERM_NAME);

      // Let's load the dialog
      let promiseSubDialogLoaded = promiseLoadSubDialog(
        "chrome://browser/content/preferences/dialogs/permissions.xhtml"
      );

      exceptionAllListButton.click();

      const win = await promiseSubDialogLoaded;

      let addButton = win.document.getElementById("btnAdd");
      Assert.ok(addButton, "Add button exists");
      Assert.ok(BrowserTestUtils.isVisible(addButton), "Add button is visible");
      Assert.ok(addButton.disabled, "Add button is disabled");

      // Now let's click the Add button to add a new exclusion
      let permissionsBox = win.document.getElementById("permissionsBox");
      let siteListUpdatedPromise = BrowserTestUtils.waitForMutationCondition(
        permissionsBox,
        { subtree: true, childList: true },
        () => {
          return permissionsBox.children.length;
        }
      );

      // Set up a mock url input value
      let urlField = win.document.getElementById("url");
      Assert.ok(urlField, "Dialog url field exists");
      const site1 = "https://example.com";
      urlField.focus();

      EventUtils.sendString(site1, win);
      Assert.ok(!addButton.disabled, "Add button is enabled");

      await addButton.updateComplete;
      addButton.click();

      await siteListUpdatedPromise;

      permissionsBox = win.document.getElementById("permissionsBox");
      Assert.equal(
        permissionsBox.children.length,
        1,
        "Should have 1 site listed as an exclusion"
      );

      let shownSite1 = permissionsBox.children[0];
      Assert.equal(
        shownSite1.getAttribute("origin"),
        site1,
        "Should match inputted site in the list of sites"
      );

      // Apply the changes
      let saveButton = win.document.querySelector("dialog").getButton("accept");
      Assert.ok(saveButton, "Save button is shown");

      saveButton.click();

      let exclusions = Services.perms.getAllByTypes([PERM_NAME]);
      Assert.equal(
        exclusions.length,
        1,
        "Should have 1 exclusion after pressing the Add button"
      );
      Assert.equal(
        exclusions[0]?.principal.siteOrigin,
        site1,
        "Should match the inputted site"
      );

      // Clean up
      Services.perms.removeByType(PERM_NAME);
      Services.prefs.clearUserPref(ONBOARDING_MESSAGE_MASK_PREF);
    }
  );
});

// Test that the exclusion_added counter is incremented
// when exclusions are added via the permissions dialog
add_task(async function test_exclusions_telemetry() {
  const PERM_NAME = "ipp-vpn";
  await setupVpnPrefs({
    feature: "beta",
    siteExceptions: true,
    entitlementCache: '{"some":"data"}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let siteExceptionsGroup = settingGroup?.querySelector(
        "#ipProtectionExceptions"
      );
      let exceptionAllListButton = siteExceptionsGroup?.querySelector(
        "#ipProtectionExceptionAllListButton"
      );

      // Clear ipp-vpn and old telemetry
      Services.perms.removeByType(PERM_NAME);
      Services.fog.testResetFOG();
      await Services.fog.testFlushAllChildren();

      // Add an existing exclusion that we'll remove later
      const site1 = "https://existing.example.com";
      let principal1 =
        Services.scriptSecurityManager.createContentPrincipalFromOrigin(site1);
      Services.perms.addFromPrincipal(
        principal1,
        PERM_NAME,
        Services.perms.DENY_ACTION
      );

      // Reset telemetry after setting up the existing exclusion
      Services.fog.testResetFOG();
      await Services.fog.testFlushAllChildren();

      // Load the permissions dialog
      let promiseSubDialogLoaded = promiseLoadSubDialog(
        "chrome://browser/content/preferences/dialogs/permissions.xhtml"
      );

      exceptionAllListButton.click();

      const win = await promiseSubDialogLoaded;

      let permissionsBox = win.document.getElementById("permissionsBox");

      // Wait for existing exclusion to appear
      await BrowserTestUtils.waitForMutationCondition(
        permissionsBox,
        { subtree: true, childList: true },
        () => permissionsBox.children.length === 1
      );

      // Add two new exclusions
      let siteListUpdatedPromise = BrowserTestUtils.waitForMutationCondition(
        permissionsBox,
        { subtree: true, childList: true },
        () => {
          return permissionsBox.children.length === 3;
        }
      );

      let urlField = win.document.getElementById("url");
      let addButton = win.document.getElementById("btnAdd");

      const site2 = "https://example.com";
      urlField.focus();
      EventUtils.sendString(site2, win);
      await addButton.updateComplete;
      addButton.click();

      const site3 = "https://another.example.com";
      urlField.focus();
      EventUtils.sendString(site3, win);
      await addButton.updateComplete;
      addButton.click();

      await siteListUpdatedPromise;

      // Remove the existing exclusion
      siteListUpdatedPromise = BrowserTestUtils.waitForMutationCondition(
        permissionsBox,
        { subtree: true, childList: true },
        () => {
          return permissionsBox.children.length === 2;
        }
      );

      let removeButton = win.document.getElementById("removePermission");
      let existingItem = Array.from(permissionsBox.children).find(
        item => item.getAttribute("origin") === site1
      );
      Assert.ok(existingItem, "Should find the existing entry");

      existingItem.click();
      await removeButton.updateComplete;
      removeButton.click();

      await siteListUpdatedPromise;

      // Apply the changes and check telemetry
      let saveButton = win.document.querySelector("dialog").getButton("accept");
      saveButton.click();

      // First verify the permissions were actually saved
      let exclusions = Services.perms.getAllByTypes([PERM_NAME]);
      Assert.equal(
        exclusions.length,
        2,
        "Should have 2 new exclusions saved and remaining, ignoring the removed existing exclusion"
      );

      await Services.fog.testFlushAllChildren();

      Assert.equal(
        Glean.ipprotection.exclusionAdded.testGetValue(),
        2,
        "exclusion_added counter should be 2, ignoring the removed existing exclusion"
      );

      // Clean up
      Services.perms.removeByType(PERM_NAME);
      Services.prefs.clearUserPref(ONBOARDING_MESSAGE_MASK_PREF);
      Services.fog.testResetFOG();
    }
  );
});

// Test that we show the correct number of site exclusions
add_task(async function test_exclusions_count() {
  const PERM_NAME = "ipp-vpn";
  await setupVpnPrefs({
    feature: "beta",
    siteExceptions: true,
    entitlementCache: '{"some":"data"}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = browser.contentDocument.querySelector(
        `setting-group[groupid="ipprotection"]`
      );
      is_element_visible(settingGroup, "ipprotection setting group is shown");

      let siteExceptionsGroup = settingGroup?.querySelector(
        "#ipProtectionExceptions"
      );
      is_element_visible(siteExceptionsGroup, "Site exceptions group is shown");

      let exceptionAllListButton = siteExceptionsGroup?.querySelector(
        "#ipProtectionExceptionAllListButton"
      );
      is_element_visible(
        exceptionAllListButton,
        "Button for list of exclusions is shown"
      );

      let sitesCountUpdatedPromise = BrowserTestUtils.waitForMutationCondition(
        exceptionAllListButton,
        { attributes: true, attributeFilter: ["data-l10n-args"] },
        () => {
          let args = exceptionAllListButton.getAttribute("data-l10n-args");
          return args && JSON.parse(args)?.count === 0;
        }
      );

      // Clear ipp-vpn to start with 0 exclusions
      Services.perms.removeByType(PERM_NAME);

      await sitesCountUpdatedPromise;

      Assert.ok(true, "Should show 0 exclusions initially");

      // Now test with 1 exclusion
      sitesCountUpdatedPromise = BrowserTestUtils.waitForMutationCondition(
        exceptionAllListButton,
        { attributes: true, attributeFilter: ["data-l10n-args"] },
        () => {
          let args = exceptionAllListButton.getAttribute("data-l10n-args");
          return args && JSON.parse(args)?.count === 1;
        }
      );
      let site1 = "https://example.com";
      let principal1 =
        Services.scriptSecurityManager.createContentPrincipalFromOrigin(site1);
      Services.perms.addFromPrincipal(
        principal1,
        PERM_NAME,
        Services.perms.DENY_ACTION
      );

      await sitesCountUpdatedPromise;

      Assert.ok(true, "Should show 1 exclusion after adding the first site");

      // Now test with 2 exclusions
      sitesCountUpdatedPromise = BrowserTestUtils.waitForMutationCondition(
        exceptionAllListButton,
        { attributes: true, attributeFilter: ["data-l10n-args"] },
        () => {
          let args = exceptionAllListButton.getAttribute("data-l10n-args");
          return args && JSON.parse(args)?.count === 2;
        }
      );
      let site2 = "https://example.org";
      let principal2 =
        Services.scriptSecurityManager.createContentPrincipalFromOrigin(site2);
      Services.perms.addFromPrincipal(
        principal2,
        PERM_NAME,
        Services.perms.DENY_ACTION
      );

      await sitesCountUpdatedPromise;

      Assert.ok(true, "Should show 2 exclusions after adding the second site");

      // Clean up
      Services.perms.removeByType(PERM_NAME);
      Services.prefs.clearUserPref(ONBOARDING_MESSAGE_MASK_PREF);
    }
  );
});

// Test that autostart checkboxes exist and map to the correct preferences
add_task(async function test_autostart_checkboxes() {
  await setupVpnPrefs({
    feature: true,
    autostartFeatureEnabled: true,
    autostart: true,
    autostartprivate: true,
    entitlementCache: '{"some":"data"}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let autoStartSettings = settingGroup?.querySelector(
        "#ipProtectionAutoStart"
      );
      is_element_visible(
        autoStartSettings,
        "autoStart settings group is shown"
      );

      let autoStartCheckbox = autoStartSettings?.querySelector(
        "#ipProtectionAutoStartCheckbox"
      );
      let autoStartPrivateCheckbox = autoStartSettings?.querySelector(
        "#ipProtectionAutoStartPrivateCheckbox"
      );

      Assert.ok(
        autoStartCheckbox.checked,
        "Autostart checkbox should be checked"
      );
      Assert.ok(
        autoStartPrivateCheckbox.checked,
        "Autostart in private browsing checkbox should be checked"
      );
    }
  );
});

// Test that additional links exist
add_task(async function test_additional_links() {
  await setupVpnPrefs({
    feature: true,
    entitlementCache: '{"subscribed": false}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let ipProtectionLinks = settingGroup?.querySelector("#ipProtectionLinks");
      is_element_visible(
        ipProtectionLinks,
        "VPN upgrade link section is shown"
      );
    }
  );
});

// Test that the "not opted in" section is shown when entitlementCache is empty
add_task(async function test_not_opted_in_section_visible_when_empty() {
  await setupVpnPrefs({
    feature: true,
    entitlementCache: "",
    siteInclusions: false,
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let notOptedInSection = settingGroup?.querySelector(
        "#ipProtectionNotOptedInSection"
      );
      is_element_visible(
        notOptedInSection,
        "Not opted in section is shown when entitlementCache is empty"
      );

      let getStartedButton = settingGroup?.querySelector("#getStartedButton");
      is_element_visible(
        getStartedButton,
        "Get started button is shown when entitlementCache is empty"
      );
    }
  );
});

// Tests that the not opted in section shows with the correct copy once site inclusions is enabled
add_task(
  async function test_not_opted_in_section_visible_with_site_inclusions_enabled() {
    await setupVpnPrefs({
      feature: true,
      entitlementCache: "",
      siteInclusions: true,
    });

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        let settingGroup = testSettingsGroupVisible(browser);
        let notOptedInSection = settingGroup?.querySelector(
          "#ipProtectionNotOptedInV2"
        );
        is_element_visible(
          notOptedInSection,
          "Not opted in section version two is shown"
        );

        let getStartedButton = settingGroup?.querySelector(
          "#getStartedButtonV2"
        );
        is_element_visible(
          getStartedButton,
          "Get started button version two is shown"
        );
      }
    );
  }
);

// Test that clicking the "Get started" button start the optin flow.
add_task(async function test_get_started_button() {
  let sandbox = sinon.createSandbox();
  sandbox
    .stub(lazy.SpecialMessageActions, "fxaSignInFlow")
    .callsFake(async function () {
      return true;
    });
  let enrollSpy = sandbox.spy(IPPDummyAuthProvider, "enroll");

  await setupVpnPrefs({
    feature: true,
    entitlementCache: "",
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let getStartedButton = settingGroup?.querySelector("#getStartedButton");
      is_element_visible(
        getStartedButton,
        "Get started button is shown when entitlementCache is empty"
      );

      const waitForPanelShown = BrowserTestUtils.waitForEvent(
        browser.documentGlobal.document,
        "popupshown",
        false,
        event => {
          if (event.target.getAttribute("viewId") === "PanelUI-ipprotection") {
            return true;
          }
          return false;
        }
      );

      getStartedButton.click();

      await waitForPanelShown;

      Assert.ok(
        lazy.SpecialMessageActions.fxaSignInFlow.calledOnce,
        "fxaSignInFlow should be called once when Get started button is clicked"
      );

      Assert.ok(
        enrollSpy.calledOnce,
        "enroll should be called once when Get started button is clicked"
      );
    }
  );

  // Clean up
  EventUtils.synthesizeKey("KEY_Escape");

  sandbox.restore();
});

// Tests flow when we click "Get started" in settings while the VPN widget
// is not visible in the toolbar.
add_task(
  async function test_get_started_button_VPN_widget_not_visible_in_toolbar() {
    let sandbox = sinon.createSandbox();
    let fxaStub = sandbox
      .stub(lazy.SpecialMessageActions, "fxaSignInFlow")
      .resolves(true);
    let enrollSpy = sandbox.spy(IPPDummyAuthProvider, "enroll");

    await setupVpnPrefs({
      feature: true,
      entitlementCache: "",
    });

    CustomizableUI.removeWidgetFromArea(lazy.IPProtectionWidget.WIDGET_ID);

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        let settingGroup = testSettingsGroupVisible(browser);
        let getStartedButton = settingGroup?.querySelector("#getStartedButton");
        is_element_visible(
          getStartedButton,
          "Get started button is shown when entitlementCache is empty"
        );

        let window = browser.documentGlobal;
        let popupSpy = sandbox.spy();
        window.document.addEventListener("popupshown", popupSpy, true);

        let enrollPromise = TestUtils.waitForCondition(
          () => enrollSpy.calledOnce,
          "enroll should be called after sign-in succeeds"
        );

        getStartedButton.click();

        await enrollPromise;

        Assert.ok(
          fxaStub.calledOnce,
          "fxaSignInFlow should still be called when widget is not visible"
        );
        Assert.ok(
          enrollSpy.calledOnce,
          "enroll should still complete when widget is not visible"
        );

        let panel = lazy.IPProtection.getPanel(window);
        Assert.ok(panel, "panel instance is created for the window");
        Assert.ok(
          !panel.active,
          "panel should not auto-open and be active when widget is not visible"
        );
        Assert.ok(
          !popupSpy.called,
          "no popup should be shown when widget is not visible"
        );

        window.document.removeEventListener("popupshown", popupSpy, true);
      }
    );

    // Restore widget
    CustomizableUI.addWidgetToArea(
      lazy.IPProtectionWidget.WIDGET_ID,
      CustomizableUI.AREA_NAVBAR
    );
    sandbox.restore();
  }
);

// Test that clicking "Get started" in settings passes vpn_integration_settings
// as the entrypoint to fxaSignInFlow.
add_task(async function test_VPN_get_started_entrypoint() {
  let sandbox = sinon.createSandbox();
  let fxaStub = sandbox
    .stub(lazy.SpecialMessageActions, "fxaSignInFlow")
    .resolves(true);

  await setupVpnPrefs({
    feature: true,
    entitlementCache: "",
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let getStartedButton = settingGroup?.querySelector("#getStartedButton");

      const waitForPanelShown = BrowserTestUtils.waitForEvent(
        browser.documentGlobal.document,
        "popupshown",
        false,
        event => event.target.getAttribute("viewId") === "PanelUI-ipprotection"
      );

      getStartedButton.click();
      await waitForPanelShown;

      Assert.ok(fxaStub.calledOnce, "fxaSignInFlow should be called once");
      Assert.equal(
        fxaStub.firstCall.args[0].entrypoint,
        "vpn_integration_settings",
        "entrypoint should be vpn_integration_settings when enrolling from settings"
      );
      Assert.equal(
        fxaStub.firstCall.args[0].extraParams.utm_source,
        "settings",
        "utm_source should be settings when enrolling from settings"
      );
    }
  );

  EventUtils.synthesizeKey("KEY_Escape");
  sandbox.restore();
});

// Test that the "not opted in" section is hidden when entitlementCache has a value
add_task(async function test_not_opted_in_section_hidden_when_opted_in() {
  await setupVpnPrefs({
    feature: true,
    entitlementCache: '{"some":"data"}',
    siteInclusions: false,
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);
      let notOptedInSection = settingGroup?.querySelector(
        "#ipProtectionNotOptedInSection"
      );
      is_element_hidden(
        notOptedInSection,
        "Not opted in section is hidden when entitlementCache has a value"
      );

      let getStartedButton = settingGroup?.querySelector("#getStartedButton");
      is_element_hidden(
        getStartedButton,
        "Get started button is hidden when entitlementCache has a value"
      );
    }
  );
});

// Test that VPN sections are hidden when entitlementCache is empty
add_task(async function test_vpn_sections_hidden_when_not_opted_in() {
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    autostartFeatureEnabled: true,
    entitlementCache: "",
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);

      let siteExceptionsGroup = settingGroup?.querySelector(
        "#ipProtectionExceptions"
      );
      is_element_hidden(
        siteExceptionsGroup,
        "Site exceptions group is hidden when entitlementCache is empty"
      );

      let autoStartSettings = settingGroup?.querySelector(
        "#ipProtectionAutoStart"
      );
      is_element_hidden(
        autoStartSettings,
        "Autostart settings group is hidden when entitlementCache is empty"
      );

      let ipProtectionLinks = settingGroup?.querySelector("#ipProtectionLinks");
      is_element_hidden(
        ipProtectionLinks,
        "VPN links section is hidden when entitlementCache is empty"
      );

      let bandwidthSettings = settingGroup?.querySelector(
        "#ipProtectionBandwidth"
      );
      is_element_hidden(
        bandwidthSettings,
        "Bandwidth settings are hidden when entitlementCache is empty"
      );
    }
  );
});

// Test that VPN sections are shown when entitlementCache has a value
add_task(async function test_vpn_sections_shown_when_opted_in() {
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    autostartFeatureEnabled: true,
    entitlementCache: '{"subscribed": false}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);

      let siteExceptionsGroup = settingGroup?.querySelector(
        "#ipProtectionExceptions"
      );
      is_element_visible(
        siteExceptionsGroup,
        "Site exceptions group is shown when entitlementCache has a value"
      );

      let autoStartSettings = settingGroup?.querySelector(
        "#ipProtectionAutoStart"
      );
      is_element_visible(
        autoStartSettings,
        "Autostart settings group is shown when entitlementCache has a value"
      );

      let ipProtectionLinks = settingGroup?.querySelector("#ipProtectionLinks");
      is_element_visible(
        ipProtectionLinks,
        "VPN links section is shown when entitlementCache has a value"
      );

      let bandwidthSettings = settingGroup?.querySelector(
        "#ipProtectionBandwidth"
      );
      is_element_hidden(
        bandwidthSettings,
        "Bandwidth settings are hidden when bandwidth feature is not enabled"
      );
    }
  );
});

// Test that the bandwidth progress bar displays the correct decimal precision
add_task(
  async function test_bandwidth_usage_decimal_precision_in_preferences() {
    // SECOND_THRESHOLD = 0.25 → 12.5 GB remaining, a clean decimal
    const remainingBytes = maxBytes * BANDWIDTH.SECOND_THRESHOLD;
    const usageCache = JSON.stringify({
      max: String(maxBytes),
      remaining: String(remainingBytes),
      reset: "2026-03-01T00:00:00Z",
    });

    await setupVpnPrefs({
      feature: true,
      bandwidth: true,
      entitlementCache: '{"some":"data"}',
      usageCache,
    });

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        let settingGroup = testSettingsGroupVisible(browser);

        let bandwidthEl = settingGroup.querySelector(
          "bandwidth-usage#ipProtectionBandwidth"
        );
        Assert.ok(bandwidthEl, "bandwidth-usage element should be present");
        is_element_visible(
          bandwidthEl,
          "bandwidth-usage element should be visible"
        );

        await bandwidthEl.updateComplete;

        Assert.equal(
          bandwidthEl.bandwidthPercent,
          75,
          "bandwidthPercent should be 75 at the second threshold"
        );
        Assert.equal(
          bandwidthEl.remainingRounded,
          remainingBytes / BANDWIDTH.BYTES_IN_GB,
          "remainingRounded should preserve the decimal GB value"
        );
      }
    );
    await SpecialPowers.popPrefEnv();
  }
);

// Test that the bandwidth progress bar displays MB when remaining is less than 1 GB
add_task(async function test_bandwidth_usage_sub_gb_precision_in_preferences() {
  const remainingBytes = Math.floor(0.9 * BANDWIDTH.BYTES_IN_GB);
  const usageCache = JSON.stringify({
    max: String(maxBytes),
    remaining: String(remainingBytes),
    reset: "2026-03-01T00:00:00Z",
  });

  await setupVpnPrefs({
    feature: true,
    bandwidth: true,
    entitlementCache: '{\"some\":\"data\"}',
    usageCache,
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);

      let bandwidthEl = settingGroup.querySelector(
        "bandwidth-usage#ipProtectionBandwidth"
      );

      Assert.ok(bandwidthEl, "bandwidth-usage element should be present");
      is_element_visible(
        bandwidthEl,
        "bandwidth-usage element should be visible"
      );

      await bandwidthEl.updateComplete;

      Assert.equal(
        bandwidthEl.bandwidthPercent,
        90,
        "bandwidthPercent should be 90 when remaining is less than 1 GB"
      );
      Assert.equal(
        bandwidthEl.remainingRounded,
        Math.floor(remainingBytes / BANDWIDTH.BYTES_IN_MB),
        "remainingRounded should be in MB when remaining is less than 1 GB"
      );
      Assert.equal(
        bandwidthEl.description.getAttribute("data-l10n-id"),
        "ip-protection-bandwidth-left-mb-1",
        "Should use the MB l10n string when remaining is less than 1 GB"
      );
    }
  );
  await SpecialPowers.popPrefEnv();
});

// Test that the upsell link is not visible if a user is considered subscribed
add_task(async function test_vpn_ipProtectionLinks_hidden_if_subscribed() {
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    entitlementCache: '{"subscribed": true}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);

      let ipProtectionLinks = settingGroup?.querySelector("#ipProtectionLinks");
      is_element_hidden(
        ipProtectionLinks,
        "VPN links section is hidden when entitlementCache.subscribed is true"
      );
    }
  );
});

// Test that the upsell link is not visible when upgradeNotAvailable is true.
add_task(
  async function test_vpn_ipProtectionLinks_hidden_if_upgrade_not_available() {
    await setupVpnPrefs({
      feature: true,
      siteExceptions: true,
      entitlementCache: '{"subscribed": false}',
      upgradeNotAvailable: true,
    });

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        let settingGroup = testSettingsGroupVisible(browser);
        let ipProtectionLinks =
          settingGroup?.querySelector("#ipProtectionLinks");
        is_element_hidden(
          ipProtectionLinks,
          "VPN upgrade link section is hidden when upgradeNotAvailable is true"
        );
      }
    );

    await SpecialPowers.popPrefEnv();
  }
);

// Test that we default to showing the upsell link if entitlement cache pref fails to be parsed.
add_task(
  async function test_vpn_ipProtectionLinks_malformed_entitlement_cache() {
    await setupVpnPrefs({
      feature: true,
      siteExceptions: true,
      entitlementCache: "invalid-json",
    });

    await BrowserTestUtils.withNewTab(
      { gBrowser, url: "about:preferences#privacy" },
      async function (browser) {
        let settingGroup = testSettingsGroupVisible(browser);

        let ipProtectionLinks =
          settingGroup?.querySelector("#ipProtectionLinks");
        is_element_visible(
          ipProtectionLinks,
          "VPN links section is shown as a fallback when entitlementCache cannot be parsed"
        );
      }
    );
  }
);

// Test the site exclusions section shows when site inclusions is false
add_task(async function test_site_rules_hidden_without_inclusions_feature() {
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    siteInclusions: false,
    entitlementCache: '{"some":"data"}',
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);

      is_element_visible(
        settingGroup?.querySelector("#ipProtectionExceptions"),
        "Site exceptions group is shown"
      );
      is_element_hidden(
        settingGroup?.querySelector("#ipProtectionSiteRules"),
        "Site rules button is hidden"
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});

// Test the site rules section stays hidden for users who have not opted in,
// even with the site inclusions feature on.
add_task(async function test_site_rules_hidden_when_not_opted_in() {
  await setupVpnPrefs({
    feature: true,
    siteExceptions: true,
    siteInclusions: true,
    entitlementCache: "",
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:preferences#privacy" },
    async function (browser) {
      let settingGroup = testSettingsGroupVisible(browser);

      is_element_hidden(
        settingGroup?.querySelector("#ipProtectionSiteRules"),
        "Site rules button is hidden when not opted in"
      );
    }
  );

  await SpecialPowers.popPrefEnv();
});
