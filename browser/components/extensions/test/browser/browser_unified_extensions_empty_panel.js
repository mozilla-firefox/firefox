/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// This file may timeout before it had a chance to complete its run in test-verify.
requestLongerTimeout(2);

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
AddonTestUtils.initMochitest(this);

const { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

loadTestSubscript("head_unified_extensions.js");

const ONBOARDING_ILLUSTRATION_URL =
  "chrome://browser/skin/addons/extensions-panel-empty-onboarding.svg";
const EMPTYSTATE_ILLUSTRATION_URL =
  "chrome://browser/skin/addons/extensions-panel-empty.svg";

// The createExtensions helper (using ExtensionTestUtils.loadExtension) does
// not support disabled add-ons. This helper uses AOM directly instead.
async function promiseInstallWebExtension(extensionData) {
  let addonFile = AddonTestUtils.createTempWebExtensionFile(extensionData);
  let { addon } = await AddonTestUtils.promiseInstallFile(addonFile);
  return addon;
}

add_setup(async function () {
  // Make sure extension buttons added to the navbar will not overflow in the
  // panel, which could happen when a previous test file resizes the current
  // window.
  await ensureMaximizedWindow(window);

  const sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());

  // The test harness registers test extensions which affects the rendered
  // button and panel. This matters especially for tests that want to verify
  // the behavior when there are no extensions to render in the list.
  // Temporarily fake-hide these extensions to ensure that we start with zero
  // extensions from the test's POV.
  async function fakeHideExtension(extensionId) {
    const { extension } = WebExtensionPolicy.getByID(extensionId);
    // This shadows ExtensionData.isHidden of the Extension subclass, causing
    // gUnifiedExtensions.getActivePolicies() to ignore the extension.
    sandbox.stub(extension, "isHidden").get(() => true);

    const addon = await AddonManager.getAddonByID(extensionId);
    sandbox.stub(addon.__AddonInternal__, "hidden").get(() => true);
  }
  await fakeHideExtension("mochikit@mozilla.org");
  await fakeHideExtension("special-powers@mozilla.org");
});

function getEmptyStateContainer(win) {
  let emptyStateBox = win.gUnifiedExtensions.panel.querySelector(
    "#unified-extensions-empty-state"
  );
  ok(emptyStateBox, "Got container for empty panel state");
  return emptyStateBox;
}

function assertIsEmptyPanelOnboardingExtensions(win) {
  const emptyStateBox = getEmptyStateContainer(win);
  ok(BrowserTestUtils.isVisible(emptyStateBox), "Empty state is visible");
  is(
    emptyStateBox.querySelector("h2").getAttribute("data-l10n-id"),
    "unified-extensions-empty-reason-zero-extensions-onboarding2",
    "Has header when the user does not have any extensions installed"
  );
  is(
    emptyStateBox.querySelector("description").getAttribute("data-l10n-id"),
    "unified-extensions-empty-content-explain-extensions-onboarding2",
    "Has description explaining extensions"
  );
  const emptyStateImgEl = emptyStateBox.querySelector("img");
  ok(
    emptyStateImgEl.classList.contains(
      win.gUnifiedExtensions.EMPTY_STATE_ILLUSTRATION_ONBOARDING_CLASS
    ),
    "Expect empty state onboarding class to be set"
  );
  ok(
    !emptyStateImgEl.classList.contains(
      win.gUnifiedExtensions.EMPTY_STATE_ILLUSTRATION_CLASS
    ),
    "Expect empty state disabled extensions class to not be set"
  );
  is(
    win.getComputedStyle(emptyStateImgEl).content,
    `url("${ONBOARDING_ILLUSTRATION_URL}")`,
    "Got the expected onboarding illustration SVG"
  );

  const discoverButton = getDiscoverButton(win);
  ok(discoverButton, "Got 'Discover button'");
  is(
    discoverButton.getAttribute("data-l10n-id"),
    "unified-extensions-discover-extensions",
    "Button in extensions panel should be labeled 'Discover Extensions'"
  );
  is(
    discoverButton.getAttribute("type"),
    "primary",
    "Discover button should be styled as a primary call-to-action button"
  );
  const manageExtensionsButton = getListView(win).querySelector(
    "#unified-extensions-manage-extensions"
  );
  ok(
    BrowserTestUtils.isHidden(manageExtensionsButton),
    "'Manage Extensions' button should be hidden"
  );
}
function getDiscoverButton(win) {
  return win.gUnifiedExtensions.panel.querySelector(
    "#unified-extensions-discover-extensions"
  );
}

async function checkManageExtensionsText(elem) {
  const l10nId = elem.dataset.l10nId;
  const doc = elem.ownerDocument;
  if (doc.hasPendingL10nMutations) {
    await BrowserTestUtils.waitForEvent(doc, "L10nMutationsFinished");
  }
  const emptyStateImgEl = elem
    .closest("#unified-extensions-empty-state")
    .querySelector("img");
  const win = doc.defaultView;
  ok(
    emptyStateImgEl.classList.contains(
      win.gUnifiedExtensions.EMPTY_STATE_ILLUSTRATION_CLASS
    ),
    "Expect empty state disabled extensions class to be set"
  );
  ok(
    !emptyStateImgEl.classList.contains(
      win.gUnifiedExtensions.EMPTY_STATE_ILLUSTRATION_ONBOARDING_CLASS
    ),
    "Expect empty state onboarding class to not be set"
  );
  is(
    win.getComputedStyle(emptyStateImgEl).content,
    `url("${EMPTYSTATE_ILLUSTRATION_URL}")`,
    "Got the expected extensions disabled illustration SVG"
  );
  const expectedButtonText = "Manage extensions";
  let expectedTextContent;
  if (l10nId === "unified-extensions-empty-content-explain-enable2") {
    expectedTextContent =
      "Select “Manage extensions” to enable them in settings.";
  } else if (l10nId === "unified-extensions-empty-content-explain-manage2") {
    expectedTextContent =
      "Select “Manage extensions” to manage them in settings.";
  } else {
    ok(false, `Unexpected data-l10n-id: ${l10nId}`);
    return;
  }
  ok(
    expectedTextContent.includes(expectedButtonText),
    "Description contains button text ('Manage extensions')"
  );
  is(expectedTextContent, elem.textContent, "Description has expected text");
}

add_task(async function test_button_opens_discopane_when_no_extension() {
  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:robots" },
    async () => {
      const { button } = gUnifiedExtensions;
      ok(button, "expected button");

      // This clicks on gUnifiedExtensions.button and waits for panel to show.
      await openExtensionsPanel(window);

      assertIsEmptyPanelOnboardingExtensions(window);
      const discoverButton = getDiscoverButton(window);

      const tabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        "about:addons",
        true
      );

      discoverButton.click();

      const tab = await tabPromise;
      is(
        gBrowser.currentURI.spec,
        "about:addons",
        "expected about:addons to be open"
      );
      is(
        gBrowser.selectedBrowser.contentWindow.gViewController.currentViewId,
        "addons://discover/",
        "expected about:addons to show the recommendations"
      );
      BrowserTestUtils.removeTab(tab);
    }
  );
});

add_task(async function test_button_opens_extlist_when_all_exts_pinned() {
  const extensions = createExtensions([
    {
      name: "Pinned extension button outside extensions panel",
      browser_action: { default_area: "navbar" },
    },
  ]);
  await Promise.all(extensions.map(extension => extension.startup()));

  await SpecialPowers.pushPrefEnv({
    set: [
      // Set this to another value to make sure not to "accidentally" land on the right page
      ["extensions.ui.lastCategory", "addons://list/theme"],
      // showPane=true is the default, but to make sure that we get the
      // expected behavior for the right reason, explicitly set it to true.
      ["extensions.getAddons.showPane", true],
    ],
  });

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:robots" },
    async () => {
      const { button } = gUnifiedExtensions;
      ok(button, "expected button");

      // Primary click should open about:addons.
      const tabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        "about:addons",
        true
      );

      button.click();

      const tab = await tabPromise;
      is(
        gBrowser.currentURI.spec,
        "about:addons",
        "expected about:addons to be open"
      );
      is(
        gBrowser.selectedBrowser.contentWindow.gViewController.currentViewId,
        "addons://list/extension",
        "expected about:addons to show the extension list"
      );
      BrowserTestUtils.removeTab(tab);
    }
  );

  await SpecialPowers.popPrefEnv();

  await Promise.all(extensions.map(extension => extension.unload()));
});

add_task(
  async function test_no_discover_button_when_pane_disabled_and_no_extension() {
    // Without the discovery pane, the empty panel keeps "Manage Extensions"
    // rather than offering "Discover extensions".
    await SpecialPowers.pushPrefEnv({
      set: [["extensions.getAddons.showPane", false]],
    });

    await openExtensionsPanel(window);

    ok(
      BrowserTestUtils.isVisible(getEmptyStateContainer(window)),
      "Empty state is visible"
    );
    ok(
      !getDiscoverButton(window),
      "'Discover extensions' button should not be present"
    );
    const manageExtensionsButton = getListView(window).querySelector(
      "#unified-extensions-manage-extensions"
    );
    ok(
      BrowserTestUtils.isVisible(manageExtensionsButton),
      "'Manage Extensions' button should be visible"
    );

    await closeExtensionsPanel(window);

    await SpecialPowers.popPrefEnv();
  }
);

add_task(async function test_button_click_in_pbm_without_any_extensions() {
  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  // This clicks on gUnifiedExtensions.button and waits for panel to show.
  await openExtensionsPanel(win);

  assertIsEmptyPanelOnboardingExtensions(win);
  const discoverButton = getDiscoverButton(win);

  // Button click opens about:addons (reuses about:privatebrowsing tab).
  // Primary click should open about:addons.
  const tabLoadedPromise = BrowserTestUtils.browserStopped(
    win.gBrowser.selectedBrowser,
    "about:addons"
  );

  discoverButton.click();

  await tabLoadedPromise;
  is(
    win.gBrowser.currentURI.spec,
    "about:addons",
    "expected about:addons to be open"
  );

  // This also closes the new tab.
  await BrowserTestUtils.closeWindow(win);
});

// Tests behavior when the user has extensions installed, but without private
// browsing access. Extensions without private browsing access are not shown,
// and if this was the only extension, then there is no extension to show.
// Instead, a message notifying the user about extensions without private
// access is shown instead.
//
// The scenario of there being an extension with private access is covered by
// test_empty_state_is_hidden_when_panel_is_non_empty below, and by
// test_list_active_extensions_only in browser_unified_extensions.js.
add_task(async function test_button_click_in_pbm_without_private_extensions() {
  const extensions = createExtensions([{ name: "Without private access" }]);
  await Promise.all(extensions.map(extension => extension.startup()));

  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  // This clicks on gUnifiedExtensions.button and waits for panel to show.
  await openExtensionsPanel(win);

  let emptyStateBox = getEmptyStateContainer(win);
  ok(BrowserTestUtils.isVisible(emptyStateBox), "Empty state is visible");
  is(
    emptyStateBox.querySelector("h2").getAttribute("data-l10n-id"),
    "unified-extensions-empty-reason-private-browsing-not-allowed",
    "Has header 'You have extensions installed, but not enabled in private windows'"
  );
  is(
    emptyStateBox.querySelector("description").getAttribute("data-l10n-id"),
    "unified-extensions-empty-content-explain-enable2",
    "Has description pointing to Manage extensions button."
  );

  await checkManageExtensionsText(emptyStateBox.querySelector("description"));

  await BrowserTestUtils.closeWindow(win);

  await Promise.all(extensions.map(extension => extension.unload()));
});

// In contrast to the above test_button_click_in_pbm_without_private_extensions
// test, this test shows that the empty panel with the private browsing message
// is hidden when there is an extension shown in the private window.
add_task(async function test_empty_state_is_hidden_when_panel_is_non_empty() {
  const extensions = [
    ...createExtensions([{ name: "Without private access" }]),
    ...createExtensions(
      [
        {
          name: "Ext with private browsing access",
          browser_specific_settings: { gecko: { id: "@ext-with-pbm-access" } },
        },
      ],
      { incognitoOverride: "spanning" }
    ),
  ];
  await Promise.all(extensions.map(extension => extension.startup()));

  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  // This clicks on gUnifiedExtensions.button and waits for panel to show.
  await openExtensionsPanel(win);

  let emptyStateBox = getEmptyStateContainer(win);
  ok(BrowserTestUtils.isHidden(emptyStateBox), "Empty state is hidden");

  // Sanity check: the second extension with PBM access is rendered (which is
  // the reason that the empty state is hidden).
  ok(
    getUnifiedExtensionsItem(extensions[1].id, win),
    "Found extension with access to PBM in panel in private window"
  );

  await BrowserTestUtils.closeWindow(win);

  await Promise.all(extensions.map(extension => extension.unload()));
});

// Verify empty state when private browsing permission is missing, but
// incognito:not_allowed is specified. See bug 1992179 for context.
add_task(async function test_button_click_in_pbm_and_incognito_not_allowed() {
  const extensions = createExtensions([
    { name: "ext with incognito:not_allowed", incognito: "not_allowed" },
  ]);
  await Promise.all(extensions.map(extension => extension.startup()));
  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  // This clicks on gUnifiedExtensions.button and waits for panel to show.
  await openExtensionsPanel(win);

  let emptyStateBox = getEmptyStateContainer(win);
  ok(BrowserTestUtils.isVisible(emptyStateBox), "Empty state is visible");
  is(
    emptyStateBox.querySelector("h2").getAttribute("data-l10n-id"),
    "unified-extensions-empty-reason-private-browsing-not-allowed",
    "Has header 'You have extensions installed, but not enabled in private windows'"
  );
  is(
    emptyStateBox.querySelector("description").getAttribute("data-l10n-id"),
    "unified-extensions-empty-content-explain-manage2",
    "Has description pointing to Manage extensions button with text MANAGE, not ENABLE"
  );

  await checkManageExtensionsText(emptyStateBox.querySelector("description"));

  await BrowserTestUtils.closeWindow(win);

  await Promise.all(extensions.map(extension => extension.unload()));
});

// Verify the behavior when there is an extension with private access but is
// pinned, and an extension without private access.
add_task(async function test_button_click_in_pbm_pinned_and_no_access() {
  const extensions = [
    ...createExtensions([{ name: "Without private access" }]),
    ...createExtensions(
      [
        {
          name: "Pinned ext with private browsing access",
          browser_action: {
            default_area: "navbar", // Pin outside extensions panel.
          },
          browser_specific_settings: { gecko: { id: "@pin-with-pbm-access" } },
        },
      ],
      { incognitoOverride: "spanning" }
    ),
  ];
  await Promise.all(extensions.map(extension => extension.startup()));
  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });

  // This clicks on gUnifiedExtensions.button and waits for panel to show.
  await openExtensionsPanel(win);

  let emptyStateBox = getEmptyStateContainer(win);
  ok(BrowserTestUtils.isVisible(emptyStateBox), "Empty state is visible");
  is(
    emptyStateBox.querySelector("h2").getAttribute("data-l10n-id"),
    "unified-extensions-empty-reason-private-browsing-not-allowed",
    "Has header 'You have extensions installed, but not enabled in private windows'"
  );
  is(
    emptyStateBox.querySelector("description").getAttribute("data-l10n-id"),
    "unified-extensions-empty-content-explain-enable2",
    "Has description pointing to Manage extensions button."
  );

  await checkManageExtensionsText(emptyStateBox.querySelector("description"));

  await BrowserTestUtils.closeWindow(win);

  await Promise.all(extensions.map(extension => extension.unload()));
});

add_task(async function test_empty_state_with_disabled_addon() {
  const [extension] = createExtensions([{ name: "The Only Extension" }]);
  await extension.startup();
  const addon = await AddonManager.getAddonByID(extension.id);
  await addon.disable();

  const win = await BrowserTestUtils.openNewBrowserWindow();

  // This clicks on gUnifiedExtensions.button and waits for panel to show.
  await openExtensionsPanel(win);

  let emptyStateBox = getEmptyStateContainer(win);
  ok(BrowserTestUtils.isVisible(emptyStateBox), "Empty state is visible");
  is(
    emptyStateBox.querySelector("h2").getAttribute("data-l10n-id"),
    "unified-extensions-empty-reason-extension-not-enabled",
    "Has header 'You have extensions installed, but not enabled'"
  );
  is(
    emptyStateBox.querySelector("description").getAttribute("data-l10n-id"),
    "unified-extensions-empty-content-explain-enable2",
    "Has description pointing to Manage extensions button."
  );

  await checkManageExtensionsText(emptyStateBox.querySelector("description"));

  await BrowserTestUtils.closeWindow(win);

  await extension.unload();
});

// This test shows that non-extension add-ons are ignored in evaluating whether
// the empty panel should be shown, even if there is another reason that could
// potentially match for extension types (e.g. add-on being disabled).
add_task(async function test_no_empty_state_with_disabled_non_extension() {
  const disabledDictAddon = await promiseInstallWebExtension({
    manifest: {
      name: "This is a dictionary (definitely not type 'extension') (disabled)",
      dictionaries: {},
      browser_specific_settings: { gecko: { id: "@dict-disabled" } },
    },
  });
  const dictAddon = await promiseInstallWebExtension({
    manifest: {
      name: "This is a dictionary (definitely not type 'extension') (enabled)",
      dictionaries: {},
      browser_specific_settings: { gecko: { id: "@dict-not-disabled" } },
    },
  });
  await disabledDictAddon.disable();
  is(disabledDictAddon.isActive, false, "One of the dict add-ons was disabled");

  await BrowserTestUtils.withNewTab(
    { gBrowser, url: "about:robots" },
    async () => {
      // This clicks on gUnifiedExtensions.button and waits for panel to show.
      await openExtensionsPanel(window);

      assertIsEmptyPanelOnboardingExtensions(window);
      const discoverButton = getDiscoverButton(window);

      const tabPromise = BrowserTestUtils.waitForNewTab(
        gBrowser,
        "about:addons",
        true
      );

      discoverButton.click();

      const tab = await tabPromise;
      ok(true, "about:addons opened instead of panel about disabled add-ons");
      BrowserTestUtils.removeTab(tab);
    }
  );

  await disabledDictAddon.uninstall();
  await dictAddon.uninstall();
});

// Verifies that if the only add-on is disabled by blocklisting, that we still
// see a panel and that the blocklist message is visible.
// Between hard block and soft blocks, the only difference is that soft block
// can be re-enabled, and that should be reflected in the message.
async function do_test_empty_state_with_blocklisted_addon(isSoftBlock) {
  const addonId = "@extension-that-is-blocked";
  const addon = await promiseInstallWebExtension({
    manifest: {
      name: "Name of the blocked ext",
      browser_specific_settings: { gecko: { id: addonId } },
    },
  });

  let promiseBlocklistAttentionUpdated = AddonTestUtils.promiseManagerEvent(
    "onBlocklistAttentionUpdated"
  );
  const cleanupBlocklist = await loadBlocklistRawData({
    [isSoftBlock ? "softblocked" : "blocked"]: [addon],
  });
  info("Wait for onBlocklistAttentionUpdated manager listener call");
  await promiseBlocklistAttentionUpdated;

  // This clicks on gUnifiedExtensions.button and waits for panel to show.
  await openExtensionsPanel(window);

  // Verify that the blocklist messages appear.
  const messages = getMessageBars(window);
  is(messages.length, 1, "Expected a message in the Extensions Panel");
  Assert.deepEqual(
    window.document.l10n.getAttributes(messages[0]),
    {
      id: isSoftBlock
        ? "unified-extensions-mb-blocklist-warning-single2"
        : "unified-extensions-mb-blocklist-error-single",
      args: {
        extensionName: "Name of the blocked ext",
        extensionsCount: 1,
      },
    },
    "Blocklist message appears in the (empty) extension panel"
  );

  let emptyStateBox = getEmptyStateContainer(window);
  ok(BrowserTestUtils.isVisible(emptyStateBox), "Empty state is visible");
  is(
    emptyStateBox.querySelector("h2").getAttribute("data-l10n-id"),
    "unified-extensions-empty-reason-extension-not-enabled",
    "Has header 'You have extensions installed, but not enabled'"
  );
  if (isSoftBlock) {
    is(
      emptyStateBox.querySelector("description").getAttribute("data-l10n-id"),
      "unified-extensions-empty-content-explain-enable2",
      "Has description pointing to Manage extensions button with text ENABLE"
    );
    await checkManageExtensionsText(emptyStateBox.querySelector("description"));
  } else {
    is(
      emptyStateBox.querySelector("description").getAttribute("data-l10n-id"),
      "unified-extensions-empty-content-explain-manage2",
      "Has description pointing to Manage extensions button with text MANAGE, not ENABLE"
    );
    await checkManageExtensionsText(emptyStateBox.querySelector("description"));
  }

  await closeExtensionsPanel(window);

  await cleanupBlocklist();

  // Verify that the messages and empty state gets cleaned up when we re-open
  // after unblocking.

  await openExtensionsPanel(window);
  is(getMessageBars().length, 0, "No blocklist messages after unblocking");
  ok(
    BrowserTestUtils.isHidden(getEmptyStateContainer(window)),
    "Empty state is hidden when extension is unblocked"
  );
  await closeExtensionsPanel(window);

  await addon.uninstall();
}

add_task(async function test_empty_state_with_blocklisted_addon_hardblock() {
  await do_test_empty_state_with_blocklisted_addon(/* isSoftBlock */ false);
});

add_task(async function test_empty_state_with_blocklisted_addon_softblock() {
  await do_test_empty_state_with_blocklisted_addon(/* isSoftBlock */ true);
});

add_task(async function test_safe_mode_notice() {
  const sandbox = sinon.createSandbox();
  registerCleanupFunction(() => sandbox.restore());

  // Services.appinfo.inSafeMode is ordinarily a constant fixed at browser
  // startup. We fake its implementation, and use a separate browser window in
  // the test to make sure that any state derived from reading the inSafeMode
  // flag is limited to this window.
  const win = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  // Services.appinfo.inSafeMode is a non-configurable property, so to spoof
  // its value we stub Services.appinfo and let it fall back to the original
  // implementation for every property, except for inSafeMode.
  const appinfoStub = new Proxy(Services.appinfo, {
    get(target, propertyKey) {
      if (propertyKey === "inSafeMode") {
        return true;
      }
      return Reflect.get(target, propertyKey, target);
    },
  });
  sandbox.stub(Services, "appinfo").get(() => appinfoStub);
  await openExtensionsPanel(win);

  const messages = getMessageBars(win);
  is(messages.length, 1, "Got one message bar");
  const bar = messages[0];
  is(bar.getAttribute("type"), "info", "Bar is informational notice");
  ok(!bar.hasAttribute("dismissable"), "Bar is not dismissable");

  const supportLink = bar.querySelector("a");
  is(
    supportLink.getAttribute("support-page"),
    "diagnose-firefox-issues-using-troubleshoot-mode",
    "expected the correct support page ID"
  );

  // We don't exactly care which empty state is shown, as the notice is
  // independent of the empty state. We just verify as a sanity check that the
  // panel is indeed empty, which is most realistic when users enter safe mode.
  let emptyStateBox = getEmptyStateContainer(win);
  ok(BrowserTestUtils.isVisible(emptyStateBox), "Empty state is visible");

  await closeExtensionsPanel(win);

  // Closing and re-opening should show one bar.
  await openExtensionsPanel(win);
  is(getMessageBars(win).length, 1, "Still one bar");
  await closeExtensionsPanel(win);

  sandbox.restore();
  is(Services.appinfo.inSafeMode, false, "Restored original inSafeMode");

  await BrowserTestUtils.closeWindow(win);
});
