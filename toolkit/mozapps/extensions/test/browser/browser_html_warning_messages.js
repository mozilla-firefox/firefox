/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint max-len: ["error", 80] */

"use strict";

let gProvider;
const { STATE_NOT_BLOCKED, STATE_BLOCKED, STATE_SOFTBLOCKED } =
  Ci.nsIBlocklistService;

const appVersion = Services.appinfo.version;
const SUPPORT_URL = Services.urlFormatter.formatURL(
  Services.prefs.getStringPref("app.support.baseURL")
);

add_setup(async function () {
  gProvider = new MockProvider();
});

async function checkAddonCard(doc, id, expected, followLink = false) {
  let card = doc.querySelector(`addon-card[addon-id="${id}"]`);
  let messageBar = card.querySelector(".addon-card-message");

  if (!expected) {
    ok(messageBar.hidden, `messagebar is hidden (addon-card ${id})`);
  } else {
    const { linkUrl, linkIsSumo, text, type } = expected;

    await BrowserTestUtils.waitForMutationCondition(
      messageBar,
      { attributes: true },
      () => !messageBar.hidden && messageBar.getAttribute("type") === type
    );
    ok(!messageBar.hidden, `messagebar is visible (addon-card ${id})`);

    is(messageBar.getAttribute("type"), type, "message has the right type");
    Assert.deepEqual(
      document.l10n.getAttributes(messageBar),
      { id: text.id, args: text.args },
      "message l10n data is set correctly"
    );

    const link = messageBar.querySelector(
      linkIsSumo ? `a[slot=support-link]` : `button[slot=actions]`
    );

    if (linkUrl) {
      ok(link, "Link element found");
      ok(BrowserTestUtils.isVisible(link), "Link is visible");
      is(
        link.getAttribute("data-l10n-id"),
        linkIsSumo ? "moz-support-link-text" : text.linkId,
        "link l10n id is correct"
      );
      if (followLink) {
        const newTab = BrowserTestUtils.waitForNewTab(gBrowser, linkUrl);
        link.click();
        BrowserTestUtils.removeTab(await newTab);
      } else {
        // Links to the blocklist details are button elements with the url
        // set on the url attribute.
        const actualLinkUrl = link.href ?? link.getAttribute("url");
        is(actualLinkUrl, linkUrl, "link should have the expected url");
      }
    } else {
      ok(!link, "Expect no slotted link element");
      is(messageBar.childElementCount, 0, "Expect no child element");
    }
  }

  return card;
}

async function checkMessageState(id, addonType, expected) {
  let win = await loadInitialView(addonType);
  let doc = win.document;

  // Check the list view.
  ok(doc.querySelector("addon-list"), "this is a list view");
  let card = await checkAddonCard(doc, id, expected, true);

  // Load the detail view.
  let loaded = waitForViewLoad(win);
  card.querySelector('[action="expand"]').click();
  await loaded;

  // Check the detail view.
  ok(!doc.querySelector("addon-list"), "this isn't a list view");
  await checkAddonCard(doc, id, expected, true);

  await closeView(win);
}

add_task(async function testNoMessageExtension() {
  let id = "no-message@mochi.test";
  let extension = ExtensionTestUtils.loadExtension({
    manifest: { browser_specific_settings: { gecko: { id } } },
    useAddonManager: "temporary",
  });
  await extension.startup();

  await checkMessageState(id, "extension", null);

  await extension.unload();
});

add_task(async function testNoMessageLangpack() {
  let id = "no-message@mochi.test";
  gProvider.createAddons([
    {
      appDisabled: true,
      id,
      name: "Signed Langpack",
      signedState: AddonManager.SIGNEDSTATE_SIGNED,
      type: "locale",
    },
  ]);

  await checkMessageState(id, "locale", null);
});

add_task(async function testHardBlocked() {
  for (const addonType of ["extension", "theme"]) {
    const id = `blocked-${addonType}@mochi.test`;
    const linkUrl = "https://example.com/addon-blocked";
    gProvider.createAddons([
      {
        appDisabled: true,
        blocklistState: STATE_BLOCKED,
        blocklistURL: linkUrl,
        id,
        name: `blocked ${addonType}`,
        type: addonType,
      },
    ]);

    let typeSuffix = addonType === "extension" ? "extension" : "other";
    await checkMessageState(id, addonType, {
      linkUrl,
      text: {
        id: `details-notification-hard-blocked-${typeSuffix}`,
        linkId: "details-notification-blocked-link2",
      },
      type: "error",
    });
  }
});

add_task(async function testSoftBlocked() {
  async function testSoftBlockedAddon({ mockAddon, expectedFluentId }) {
    const [testAddon] = gProvider.createAddons([
      {
        appDisabled: false,
        blocklistState: STATE_SOFTBLOCKED,
        ...mockAddon,
      },
    ]);
    await checkMessageState(mockAddon.id, mockAddon.type ?? "extension", {
      linkUrl: mockAddon.blocklistURL,
      text: {
        id: expectedFluentId,
        args: null,
        linkId: "details-notification-softblocked-link2",
      },
      type: "warning",
    });
    await testAddon.uninstall();
  }

  // Verify soft-block message on a softdisabled extension and theme.
  await testSoftBlockedAddon({
    expectedFluentId: "details-notification-soft-blocked-extension-disabled2",
    mockAddon: {
      id: "softblocked-extension@mochi.test",
      name: "Soft-Blocked Extension",
      type: "extension",
      blocklistURL: "https://example.com/addon-blocked",
      softDisabled: true,
    },
  });
  await testSoftBlockedAddon({
    expectedFluentId: "details-notification-soft-blocked-other-disabled2",
    mockAddon: {
      id: "softblocked-theme@mochi.test",
      name: "Soft-Blocked Theme",
      type: "theme",
      blocklistURL: "https://example.com/addon-blocked",
      softDisabled: true,
    },
  });

  // Verify soft-block message on a re-enabled extension and theme.
  await testSoftBlockedAddon({
    expectedFluentId: "details-notification-soft-blocked-extension-enabled2",
    mockAddon: {
      id: "softblocked-extension@mochi.test",
      name: "Soft-Blocked Extension",
      type: "extension",
      blocklistURL: "https://example.com/addon-blocked",
      userDisabled: false,
    },
  });
  await testSoftBlockedAddon({
    expectedFluentId: "details-notification-soft-blocked-other-enabled2",
    mockAddon: {
      id: "softblocked-theme@mochi.test",
      name: "Soft-Blocked Theme",
      type: "theme",
      blocklistURL: "https://example.com/addon-blocked",
      userDisabled: false,
    },
  });
});

add_task(async function testUnsignedDisabled() {
  // This pref being disabled will cause the `specialpowers` addon to be
  // uninstalled, which can cause a number of test failures due to features no
  // longer working correctly.
  // In order to avoid those issues, this code manually disables the pref, and
  // ensures that `SpecialPowers` is fully re-enabled at the end of the test.
  const sigPref = "xpinstall.signatures.required";
  Services.prefs.setBoolPref(sigPref, true);

  const id = "unsigned@mochi.test";
  const name = "Unsigned";
  gProvider.createAddons([
    {
      appDisabled: true,
      id,
      name,
      signedState: AddonManager.SIGNEDSTATE_MISSING,
    },
  ]);
  await checkMessageState(id, "extension", {
    linkUrl: SUPPORT_URL + "unsigned-addons",
    linkIsSumo: true,
    text: {
      id: "details-notification-unsigned-and-disabled2",
      args: { name },
    },
    type: "error",
  });

  // Ensure that `SpecialPowers` is fully re-initialized at the end of this
  // test. This requires removing the existing binding so that it's
  // re-registered, re-enabling unsigned extensions, and then waiting for the
  // actor to be registered and ready.
  delete window.SpecialPowers;
  Services.prefs.setBoolPref(sigPref, false);
  await TestUtils.waitForCondition(() => {
    try {
      return !!windowGlobalChild.getActor("SpecialPowers");
    } catch (e) {
      return false;
    }
  }, "wait for SpecialPowers to be reloaded");
  ok(window.SpecialPowers, "SpecialPowers should be re-defined");
});

add_task(async function testUnsignedLangpackDisabled() {
  const id = "unsigned-langpack@mochi.test";
  const name = "Unsigned";
  gProvider.createAddons([
    {
      appDisabled: true,
      id,
      name,
      signedState: AddonManager.SIGNEDSTATE_MISSING,
      type: "locale",
    },
  ]);
  await checkMessageState(id, "locale", {
    linkUrl: SUPPORT_URL + "unsigned-addons",
    linkIsSumo: true,
    text: {
      id: "details-notification-unsigned-and-disabled2",
      args: { name },
    },
    type: "error",
  });
});

add_task(async function testIncompatible() {
  const id = "incompatible@mochi.test";
  const name = "Incompatible";
  gProvider.createAddons([
    {
      appDisabled: true,
      id,
      isActive: false,
      isCompatible: false,
      name,
    },
  ]);
  await checkMessageState(id, "extension", {
    text: {
      id: "details-notification-incompatible2",
      args: { name, version: appVersion },
    },
    type: "error",
  });
});

add_task(async function testUnsignedEnabled() {
  await SpecialPowers.pushPrefEnv({
    // Ensure unsigned add-on warnings are enabled while running this
    // test also on Thunderbird builds where it is set to true by default.
    set: [["extensions.ui.disableUnsignedWarnings", false]],
  });
  const id = "unsigned-allowed@mochi.test";
  const name = "Unsigned";
  gProvider.createAddons([
    {
      id,
      name,
      signedState: AddonManager.SIGNEDSTATE_MISSING,
    },
  ]);
  await checkMessageState(id, "extension", {
    linkUrl: SUPPORT_URL + "unsigned-addons",
    linkIsSumo: true,
    text: { id: "details-notification-unsigned2", args: { name } },
    type: "warning",
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function testUnsignedLangpackEnabled() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.langpacks.signatures.required", false],
      // Ensure unsigned add-on warnings are enabled while running this
      // test also on Thunderbird builds where it is set to true by default.
      ["extensions.ui.disableUnsignedWarnings", false],
    ],
  });

  const id = "unsigned-allowed-langpack@mochi.test";
  const name = "Unsigned Langpack";
  gProvider.createAddons([
    {
      id,
      name,
      signedState: AddonManager.SIGNEDSTATE_MISSING,
      type: "locale",
    },
  ]);
  await checkMessageState(id, "locale", {
    linkUrl: SUPPORT_URL + "unsigned-addons",
    linkIsSumo: true,
    text: { id: "details-notification-unsigned2", args: { name } },
    type: "warning",
  });

  await SpecialPowers.popPrefEnv();
});

add_task(async function testPluginInstalling() {
  const id = "plugin-installing@mochi.test";
  const name = "Plugin Installing";
  gProvider.createAddons([
    {
      id,
      isActive: true,
      isGMPlugin: true,
      isInstalled: false,
      name,
      type: "plugin",
    },
  ]);
  await checkMessageState(id, "plugin", {
    text: { id: "details-notification-gmp-pending2", args: { name } },
    type: "warning",
  });
});

add_task(async function testCardRefreshedOnBlocklistStateChanges() {
  const { AddonTestUtils } = ChromeUtils.importESModule(
    "resource://testing-common/AddonTestUtils.sys.mjs"
  );

  let needsCleanupBlocklist = true;
  const cleanupBlocklist = async () => {
    if (!needsCleanupBlocklist) {
      return;
    }
    await AddonTestUtils.loadBlocklistRawData({
      extensionsMLBF: [],
    });
    needsCleanupBlocklist = false;
  };
  registerCleanupFunction(cleanupBlocklist);

  // This test:
  // - does not use the MockProvider to also verify that
  //   the XPIProvider is emitting calls to the onPropertyChanged
  //   addon listeners on blocklistState changes.
  // - uses a signed xpi to ensure the addon card messagebar for unsigned
  //   addons is not shown.
  const id = "amosigned-xpi@tests.mozilla.org";
  const version = "2.2";
  const XPI_URL = `${TESTROOT}../xpinstall/amosigned.xpi`;
  let install = await AddonManager.getInstallForURL(XPI_URL, {
    id,
    type: "extension",
    version,
  });
  await install.install();
  const addon = await AddonManager.getAddonByID(id);

  await checkMessageState(id, "extension", null);

  // Open the about:addons list view and keep it open to verify
  // it is being refreshed when the addon blocklistState is
  // expected to change
  let win = await loadInitialView("extension");
  let doc = win.document;
  ok(doc.querySelector("addon-list"), "this is a list view");

  // Sanity checks:
  // - addon blocklistState initially set to STATE_NOT_BLOCKED.
  // - addon card message bar should be hidden.
  Assert.equal(
    addon.blocklistState,
    STATE_NOT_BLOCKED,
    "Expect test extension to NOT be initially blocked"
  );
  await checkAddonCard(doc, id, null);

  // We intentionally turn off this a11y check, because the following click
  // is purposefully targeting a non-interactive element to clear the focused
  // state with a mouse which can be done by assistive technology and keyboard
  // by pressing `Esc`, this rule check shall be ignored by a11y_checks suite.
  AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
  // Click outside the list to clear any focus (needed to ensure the test will
  // not get stuck forever waiting for the "move" event to be dispatched on
  // the addon-list custom element).
  EventUtils.synthesizeMouseAtCenter(
    doc.querySelector(".header-name"),
    {},
    win
  );
  AccessibilityUtils.resetEnv();

  let moved = BrowserTestUtils.waitForEvent(
    doc.querySelector("addon-list"),
    "move"
  );
  await addon.disable();
  await moved;

  info("Verify blocklistState changing from unblocked to hard-blocked");

  const blockKey = `${id}:${version}`;
  const waitForBlocklistStateChanged = () =>
    AddonTestUtils.promiseAddonEvent(
      "onPropertyChanged",
      (addon, changedProps) =>
        addon.id === id && changedProps.includes("blocklistState")
    );

  let promiseBlocklistStateChanged = waitForBlocklistStateChanged();
  await AddonTestUtils.loadBlocklistRawData({
    extensionsMLBF: [
      {
        stash: {
          blocked: [blockKey],
          softblocked: [],
          unblocked: [],
        },
      },
    ],
  });
  await promiseBlocklistStateChanged;

  const baseDetailsURL =
    "https://addons.mozilla.org/en-US/firefox/blocked-addon";
  const linkUrl = `${baseDetailsURL}/${id}/${version}/`;

  await checkAddonCard(doc, id, {
    linkUrl,
    text: {
      id: `details-notification-hard-blocked-extension`,
      linkId: "details-notification-blocked-link2",
    },
    type: "error",
  });

  info("Verify blocklistState changing from hard-blocked to soft-blocked");

  promiseBlocklistStateChanged = waitForBlocklistStateChanged();
  await AddonTestUtils.loadBlocklistRawData({
    extensionsMLBF: [
      {
        stash: {
          blocked: [],
          softblocked: [blockKey],
          unblocked: [],
        },
      },
    ],
  });
  await promiseBlocklistStateChanged;

  await checkAddonCard(doc, id, {
    linkUrl,
    text: {
      id: `details-notification-soft-blocked-extension-disabled2`,
      linkId: "details-notification-softblocked-link2",
    },
    type: "warning",
  });

  info("Enable soft-blocked addon");
  moved = BrowserTestUtils.waitForEvent(
    doc.querySelector("addon-list"),
    "move"
  );
  await addon.enable();
  await moved;

  await checkAddonCard(doc, id, {
    linkUrl,
    text: {
      id: `details-notification-soft-blocked-extension-enabled2`,
      linkId: "details-notification-softblocked-link2",
    },
    type: "warning",
  });

  info("Verify blocklistState changing from soft-blocked to not-blocked");

  promiseBlocklistStateChanged = waitForBlocklistStateChanged();
  await AddonTestUtils.loadBlocklistRawData({
    extensionsMLBF: [
      {
        stash: {
          blocked: [],
          softblocked: [],
          unblocked: [blockKey],
        },
      },
    ],
  });
  await promiseBlocklistStateChanged;

  await checkAddonCard(doc, id, null);

  await closeView(win);
  await addon.uninstall();

  await cleanupBlocklist();
});
