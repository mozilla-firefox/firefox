const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { Management } = ChromeUtils.importESModule(
  "resource://gre/modules/Extension.sys.mjs"
);
const { ExtensionPermissions } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissions.sys.mjs"
);
const { PERMISSION_L10N, PERMISSION_L10N_ID_OVERRIDES } =
  ChromeUtils.importESModule(
    "resource://gre/modules/ExtensionPermissionMessages.sys.mjs"
  );

AddonTestUtils.initMochitest(this);

const assertVisibleSections = async (permsSection, expectedHeaders) => {
  let headers = Array.from(permsSection.querySelectorAll(".permission-header"))
    // Filter out hidden sections.
    .filter(el => !el.parentNode.hidden)
    .map(el => el.textContent);
  Assert.deepEqual(
    headers,
    await Promise.all(
      expectedHeaders.map(id => permsSection.ownerDocument.l10n.formatValue(id))
    ),
    "Got expected headers"
  );
};

async function background() {
  browser.permissions.onAdded.addListener(perms => {
    if (localStorage.getItem("listening")) {
      browser.test.sendMessage("permission-added", perms);
    } else {
      browser.test.log(
        `permissions-added before listening ${JSON.stringify({
          id: browser.runtime.id,
          perms,
        })}`
      );
    }
  });
  browser.permissions.onRemoved.addListener(perms => {
    browser.test.sendMessage("permission-removed", perms);
  });

  browser.test.onMessage.addListener(async _ => {
    localStorage.setItem("listening", true);
    browser.test.sendMessage("ready");
  });
}

async function getExtensions({ manifest_version = 2, expectGranted } = {}) {
  let extensions = {
    "addon0@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 0",
        browser_specific_settings: { gecko: { id: "addon0@mochi.test" } },
        permissions: ["alarms", "contextMenus"],
      },
      background,
      useAddonManager: "temporary",
    }),
    "addon1@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 1",
        browser_specific_settings: { gecko: { id: "addon1@mochi.test" } },
        permissions: ["alarms", "contextMenus", "tabs", "webNavigation"],
        // Note: for easier testing, we merge host_permissions into permissions
        // when loading mv2 extensions, see ExtensionTestCommon.generateFiles.
        host_permissions: ["<all_urls>", "file://*/*"],
      },
      background,
      useAddonManager: "temporary",
    }),
    "addon2@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 2",
        browser_specific_settings: { gecko: { id: "addon2@mochi.test" } },
        permissions: ["alarms", "contextMenus"],
        optional_permissions: ["http://mochi.test/*"],
      },
      background,
      useAddonManager: "temporary",
    }),
    "addon3@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 3",
        version: "1.0",
        browser_specific_settings: { gecko: { id: "addon3@mochi.test" } },
        permissions: ["tabs"],
        optional_permissions: ["webNavigation", "<all_urls>"],
      },
      background,
      useAddonManager: "temporary",
    }),
    "addon4@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 4",
        browser_specific_settings: { gecko: { id: "addon4@mochi.test" } },
        optional_permissions: ["tabs", "webNavigation"],
      },
      background,
      useAddonManager: "temporary",
    }),
    "addon5@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 5",
        browser_specific_settings: { gecko: { id: "addon5@mochi.test" } },
        optional_permissions: ["*://*/*"],
      },
      background,
      useAddonManager: "temporary",
    }),
    "priv6@mochi.test": ExtensionTestUtils.loadExtension({
      isPrivileged: true,
      manifest: {
        manifest_version,
        name: "Privileged add-on 6",
        browser_specific_settings: { gecko: { id: "priv6@mochi.test" } },
        optional_permissions: [
          "file://*/*",
          "about:reader*",
          "resource://pdf.js/*",
          "*://*.mozilla.com/*",
          "*://*/*",
          "<all_urls>",
        ],
      },
      background,
      useAddonManager: "temporary",
    }),
    "addon7@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 7",
        browser_specific_settings: { gecko: { id: "addon7@mochi.test" } },
        optional_permissions: ["<all_urls>", "https://*/*", "file://*/*"],
      },
      background,
      useAddonManager: "temporary",
    }),
    "addon8@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 8",
        browser_specific_settings: { gecko: { id: "addon8@mochi.test" } },
        // eslint-disable-next-line @microsoft/sdl/no-insecure-url
        optional_permissions: ["https://*/*", "http://*/*", "file://*/*"],
      },
      background,
      useAddonManager: "temporary",
    }),
    "other@mochi.test": ExtensionTestUtils.loadExtension({
      manifest: {
        manifest_version,
        name: "Test add-on 6",
        browser_specific_settings: { gecko: { id: "other@mochi.test" } },
        optional_permissions: [
          "tabs",
          "webNavigation",
          "<all_urls>",
          "*://*/*",
        ],
      },
      useAddonManager: "temporary",
    }),
  };
  for (let [id, ext] of Object.entries(extensions)) {
    let promiseGranted;

    // We need to start listening for changes only after we get the first
    // `change-permissions` event to avoid intermittent events from initial
    // granting of origin permissions in mv3 on startup.

    // This can happen because we're not awaiting in _setupStartupPermissions:
    // https://searchfox.org/mozilla-central/rev/55944eaee1/toolkit/components/extensions/Extension.sys.mjs#3694-3697

    if (manifest_version >= 3 && expectGranted && id === "addon1@mochi.test") {
      promiseGranted = new Promise(resolve => {
        info(`Waiting for ${id} host permissions to be granted.`);
        Management.on("change-permissions", function listener(_, e) {
          info(`Got change-permissions event: ${JSON.stringify(e)}`);
          if (e.extensionId === id && e.added?.origins?.length) {
            Management.off("change-permissions", listener);
            resolve();
          }
        });
      });
    }

    await ext.startup();
    await promiseGranted;

    if (id !== "other@mochi.test") {
      ext.sendMessage("init");
      await ext.awaitMessage("ready");
    }
  }
  return extensions;
}

function waitForPermissionChange(id) {
  return new Promise(resolve => {
    info(`listening for change on ${id}`);
    let listener = (type, data) => {
      info(`change permissions ${JSON.stringify(data)}`);
      if (data.extensionId !== id) {
        return;
      }
      ExtensionPermissions.removeListener(listener);
      resolve(data);
    };
    ExtensionPermissions.addListener(listener);
  });
}

async function runTest(options) {
  let {
    extension,
    addonId,
    permissions = [],
    optional_permissions = [],
    optional_overlapping = [],
    optional_enabled = [],
    // Map<permission->string> to check optional_permissions against, if set.
    optional_strings = {},
    view,
  } = options;
  if (extension) {
    addonId = extension.id;
  }

  let win = view || (await loadInitialView("extension"));

  let card = getAddonCard(win, addonId);
  let permsSection = card.querySelector("addon-permissions-list");
  if (!permsSection) {
    ok(!card.hasAttribute("expanded"), "The list card is not expanded");
    let loaded = waitForViewLoad(win);
    card.querySelector('[action="expand"]').click();
    await loaded;
  }

  card = getAddonCard(win, addonId);
  let { deck, tabGroup } = card.details;

  // Make sure the card is fully localized.
  await win.document.l10n.translateFragment(card);

  let permsBtn = tabGroup.querySelector('[name="permissions"]');
  let permsShown = BrowserTestUtils.waitForEvent(deck, "view-changed");
  permsBtn.click();
  await permsShown;

  permsSection = card.querySelector("addon-permissions-list");

  let rows = Array.from(permsSection.querySelectorAll(".addon-detail-row"));
  let permission_rows = Array.from(
    permsSection.querySelectorAll(".permission-info")
  );

  // Last row is the learn more link.
  info("Check learn more link");
  let link = rows[rows.length - 1].firstElementChild;
  let rootUrl = Services.urlFormatter.formatURLPref("app.support.baseURL");
  let url = rootUrl + "extension-permissions";
  is(link.href, url, "The URL is set");
  is(link.getAttribute("target"), "_blank", "The link opens in a new tab");

  // We should have one more row (learn more) that the combined permissions,
  // or if no permissions, 2 rows.
  let num_permissions = permissions.length + optional_permissions.length;
  is(
    permission_rows.length,
    num_permissions,
    "correct number of details rows are present"
  );

  info("Check displayed permissions");
  if (!num_permissions) {
    is(
      win.document.l10n.getAttributes(rows[0]).id,
      "addon-permissions-empty2",
      "There's a message when no permissions are shown"
    );
  }

  // Check visible sections, making sure we don't show any unwanted one (e.g.
  // data collection sections when it is turned off).
  let expectedHeaders = [];
  if (permissions.length) {
    expectedHeaders.push("addon-permissions-required");
  }
  if (optional_permissions.length) {
    expectedHeaders.push("addon-permissions-optional");
  }
  await assertVisibleSections(permsSection, expectedHeaders);

  if (permissions.length) {
    for (let name of permissions) {
      // Check the permission-info class to make sure it's for a permission.
      let row = permission_rows.shift();
      ok(
        row.classList.contains("permission-info"),
        `required permission row for ${name}`
      );
    }
  }

  let addon = await AddonManager.getAddonByID(addonId);
  info(`addon ${addon.id} is ${addon.userDisabled ? "disabled" : "enabled"}`);

  // This tests the permission change and button state when the user
  // changes the state in about:addons.
  async function testTogglePermissionButton(
    permissions,
    button,
    excpectDisabled = false
  ) {
    let enabled = permissions.some(perm => optional_enabled.includes(perm));
    if (excpectDisabled) {
      enabled = !enabled;
    }
    is(
      button.pressed,
      enabled,
      `permission is set correctly for ${permissions}: ${button.pressed}`
    );
    let change;
    if (addon.userDisabled || !extension) {
      change = waitForPermissionChange(addonId);
    } else if (!enabled) {
      change = extension.awaitMessage("permission-added");
    } else {
      change = extension.awaitMessage("permission-removed");
    }

    button.click();

    let perms = await change;
    if (addon.userDisabled || !extension) {
      perms = enabled ? perms.removed : perms.added;
    }

    Assert.greater(
      perms.permissions.length + perms.origins.length,
      0,
      "Some permission(s) toggled."
    );

    if (perms.permissions.length) {
      // Only check api permissions against the first passed permission,
      // because we treat <all_urls> as an api permission, but not *://*/*.
      is(perms.permissions.length, 1, "A single api permission toggled.");
      is(perms.permissions[0], permissions[0], "Correct api permission.");
    }
    if (perms.origins.length) {
      Assert.deepEqual(
        perms.origins.slice().sort(),
        permissions.slice().sort(),
        "Toggled origin permission."
      );
    }

    await BrowserTestUtils.waitForCondition(async () => {
      return button.pressed == !enabled;
    }, "button changed state");
  }

  // This tests that the button changes state if the permission is
  // changed outside of about:addons
  async function testExternalPermissionChange(permission, button) {
    let enabled = button.pressed;
    let type = button.getAttribute("permission-type");
    let change;
    if (addon.userDisabled || !extension) {
      change = waitForPermissionChange(addonId);
    } else if (!enabled) {
      change = extension.awaitMessage("permission-added");
    } else {
      change = extension.awaitMessage("permission-removed");
    }

    let permissions = { permissions: [], origins: [], data_collection: [] };
    if (type == "origin") {
      permissions.origins = [permission];
    } else {
      permissions.permissions = [permission];
    }

    if (enabled) {
      await ExtensionPermissions.remove(addonId, permissions);
    } else {
      await ExtensionPermissions.add(addonId, permissions);
    }

    let perms = await change;
    if (addon.userDisabled || !extension) {
      perms = enabled ? perms.removed : perms.added;
    }
    ok(
      perms.permissions.includes(permission) ||
        perms.origins.includes(permission),
      "permission was toggled"
    );

    await BrowserTestUtils.waitForCondition(async () => {
      return button.pressed == !enabled;
    }, "button changed state");
  }

  // This tests that changing the permission on another addon does
  // not change the UI for the addon we're testing.
  async function testOtherPermissionChange(permission, toggle) {
    let type = toggle.getAttribute("permission-type");
    let otherId = "other@mochi.test";
    let change = waitForPermissionChange(otherId);
    let perms = await ExtensionPermissions.get(otherId);
    let existing = type == "origin" ? perms.origins : perms.permissions;
    let permissions = { permissions: [], origins: [], data_collection: [] };
    if (type == "origin") {
      permissions.origins = [permission];
    } else {
      permissions.permissions = [permission];
    }

    if (existing.includes(permission)) {
      await ExtensionPermissions.remove(otherId, permissions);
    } else {
      await ExtensionPermissions.add(otherId, permissions);
    }
    await change;
  }

  if (optional_permissions.length) {
    for (let name of optional_permissions) {
      // Set of permissions represented by this key.
      let perms = [name];
      if (name === optional_overlapping[0]) {
        perms = optional_overlapping;
      }

      // Check the row is a permission row with the correct key on the toggle
      // control.
      let row = permission_rows.shift();
      let toggle = row.querySelector("moz-toggle");
      let label = toggle.labelEl;

      let str = optional_strings[name];
      if (str) {
        is(label.textContent.trim(), str, `Expected permission string ${str}`);
      }

      ok(
        row.classList.contains("permission-info"),
        `optional permission row for ${name}`
      );
      is(
        toggle.getAttribute("permission-key"),
        name,
        `optional permission toggle exists for ${name}`
      );

      await testTogglePermissionButton(perms, toggle);
      await testTogglePermissionButton(perms, toggle, true);

      for (let perm of perms) {
        // make a change "outside" the UI and check the values.
        // toggle twice to test both add/remove.
        await testExternalPermissionChange(perm, toggle);
        // change another addon to mess around with optional permission
        // values to see if it effects the addon we're testing here.  The
        // next check would fail if anything bleeds onto other addons.
        await testOtherPermissionChange(perm, toggle);
        // repeat the "outside" test.
        await testExternalPermissionChange(perm, toggle);
      }
    }
  }

  if (!view) {
    await closeView(win);
  }
}

async function testPermissionsView({
  manifestV3enabled,
  manifest_version,
  expectGranted,
}) {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.manifestV3.enabled", manifestV3enabled]],
  });

  // pre-set a permission prior to starting extensions.
  await ExtensionPermissions.add("addon4@mochi.test", {
    permissions: ["tabs"],
    origins: [],
  });

  let extensions = await getExtensions({ manifest_version, expectGranted });

  info("Check add-on with required permissions");
  if (manifest_version < 3) {
    await runTest({
      extension: extensions["addon1@mochi.test"],
      permissions: ["<all_urls>", "tabs", "webNavigation"],
    });
  }
  if (manifest_version >= 3 && expectGranted) {
    await runTest({
      extension: extensions["addon1@mochi.test"],
      permissions: ["tabs", "webNavigation"],
      optional_permissions: ["<all_urls>"],
      optional_enabled: ["<all_urls>"],
    });
  }
  if (manifest_version >= 3 && !expectGranted) {
    await runTest({
      extension: extensions["addon1@mochi.test"],
      permissions: ["tabs", "webNavigation"],
      optional_permissions: ["<all_urls>"],
    });
  }

  info("Check add-on without any displayable permissions");
  await runTest({ extension: extensions["addon0@mochi.test"] });

  info("Check add-on with only one optional origin.");
  await runTest({
    extension: extensions["addon2@mochi.test"],
    optional_permissions: manifestV3enabled ? ["http://mochi.test/*"] : [],
    optional_strings: {
      "http://mochi.test/*": "Access your data for http://mochi.test",
    },
  });

  info("Check add-on with both required and optional permissions");
  await runTest({
    extension: extensions["addon3@mochi.test"],
    permissions: ["tabs"],
    optional_permissions: ["webNavigation", "<all_urls>"],
  });

  // Grant a specific optional host permission not listed in the manifest.
  await ExtensionPermissions.add("addon3@mochi.test", {
    permissions: [],
    origins: ["https://example.com/*"],
  });
  await extensions["addon3@mochi.test"].awaitMessage("permission-added");

  info("Check addon3 again and expect the new optional host permission");
  await runTest({
    extension: extensions["addon3@mochi.test"],
    permissions: ["tabs"],
    optional_permissions: [
      "webNavigation",
      "<all_urls>",
      ...(manifestV3enabled ? ["https://example.com/*"] : []),
    ],
    optional_enabled: ["https://example.com/*"],
    optional_strings: {
      "https://example.com/*": "Access your data for https://example.com",
    },
  });

  info("Check add-on with only optional permissions, tabs is pre-enabled");
  await runTest({
    extension: extensions["addon4@mochi.test"],
    optional_permissions: ["tabs", "webNavigation"],
    optional_enabled: ["tabs"],
  });

  info("Check add-on with a global match pattern in place of all urls");
  await runTest({
    extension: extensions["addon5@mochi.test"],
    optional_permissions: ["*://*/*"],
  });

  info("Check privileged add-on with non-web origin permissions");
  await runTest({
    extension: extensions["priv6@mochi.test"],
    optional_permissions: [
      "<all_urls>",
      ...(manifestV3enabled ? ["*://*.mozilla.com/*"] : []),
    ],
    optional_overlapping: ["<all_urls>", "*://*/*"],
    optional_strings: {
      "*://*.mozilla.com/*":
        "Access your data for sites in the *://mozilla.com domain",
    },
  });

  info(`Check that <all_urls> is used over other "all websites" permissions`);
  await runTest({
    extension: extensions["addon7@mochi.test"],
    optional_permissions: ["<all_urls>"],
    optional_overlapping: ["<all_urls>", "https://*/*"],
  });

  info(`Also check different "all sites" permissions in the manifest`);
  await runTest({
    extension: extensions["addon8@mochi.test"],
    optional_permissions: ["https://*/*"],
    // eslint-disable-next-line @microsoft/sdl/no-insecure-url
    optional_overlapping: ["https://*/*", "http://*/*"],
  });

  for (let ext of Object.values(extensions)) {
    await ext.unload();
  }

  await SpecialPowers.popPrefEnv();
}

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    // TODO: Bug 1960273 - Update this test and remove this pref set when we
    // enable the data collection permissions on all channels.
    set: [["extensions.dataCollectionPermissions.enabled", false]],
  });
});

add_task(async function testPermissionsView_MV2_manifestV3disabled() {
  await testPermissionsView({ manifestV3enabled: false, manifest_version: 2 });
});

add_task(async function testPermissionsView_MV2_manifestV3enabled() {
  await testPermissionsView({ manifestV3enabled: true, manifest_version: 2 });
});

add_task(async function testPermissionsView_MV3_noInstallPrompt() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.originControls.grantByDefault", false]],
  });
  await testPermissionsView({
    manifestV3enabled: true,
    manifest_version: 3,
    expectGranted: false,
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function testPermissionsView_MV3() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.originControls.grantByDefault", true]],
  });
  await testPermissionsView({
    manifestV3enabled: true,
    manifest_version: 3,
    expectGranted: true,
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function testPermissionsViewStates() {
  let ID = "addon@mochi.test";

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test add-on 3",
      version: "1.0",
      browser_specific_settings: { gecko: { id: ID } },
      permissions: ["tabs"],
      optional_permissions: ["webNavigation", "<all_urls>"],
    },
    useAddonManager: "permanent",
  });
  await extension.startup();

  info(
    "Check toggling permissions on a disabled addon with addon3@mochi.test."
  );
  let view = await loadInitialView("extension");
  let addon = await AddonManager.getAddonByID(ID);
  await addon.disable();
  ok(addon.userDisabled, "addon is disabled");
  await runTest({
    extension,
    permissions: ["tabs"],
    optional_permissions: ["webNavigation", "<all_urls>"],
    view,
  });
  await addon.enable();
  ok(!addon.userDisabled, "addon is enabled");

  async function install_addon(extensionData) {
    let xpi = await AddonTestUtils.createTempWebExtensionFile(extensionData);
    let { addon } = await AddonTestUtils.promiseInstallFile(xpi);
    return addon;
  }

  function wait_for_addon_item_updated(addonId) {
    return BrowserTestUtils.waitForEvent(getAddonCard(view, addonId), "update");
  }

  let promiseItemUpdated = wait_for_addon_item_updated(ID);
  addon = await install_addon({
    manifest: {
      name: "Test add-on 3",
      version: "2.0",
      browser_specific_settings: { gecko: { id: ID } },
      optional_permissions: ["webNavigation"],
    },
    useAddonManager: "permanent",
  });
  is(addon.version, "2.0", "addon upgraded");
  await promiseItemUpdated;

  await runTest({
    addonId: addon.id,
    optional_permissions: ["webNavigation"],
    view,
  });

  // While the view is still available, test setting a permission
  // that is not in the manifest of the addon.
  let card = getAddonCard(view, addon.id);
  await Assert.rejects(
    card.setAddonPermission("webRequest", "permission", "add"),
    /was not declared in optional_permissions/,
    "unable to set the addon permission"
  );

  await closeView(view);
  await extension.unload();
});

add_task(async function testTempOrigins() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.webextOptionalPermissionPrompts", false]],
  });

  const addonId = "temp@mochi.test";
  const originA = "*://a.com/*";
  const originB = "*://b.net/*";

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version: 3,
      browser_specific_settings: { gecko: { id: addonId } },
      optional_permissions: ["<all_urls>"],
    },
    useAddonManager: "permanent",
    background() {
      browser.test.onMessage.addListener(origins => {
        browser.test.withHandlingUserInput(() => {
          browser.permissions.request({ origins });
        });
      });
    },
  });

  async function checkExpected(origins, granted) {
    let view = await loadInitialView("extension");
    await runTest({
      addonId,
      optional_permissions: ["<all_urls>", ...origins],
      optional_enabled: granted,
      view,
    });
    await closeView(view);
  }

  await extension.startup();

  info("Checking before any runtime permission requests.");
  await checkExpected([], []);

  let grantA = waitForPermissionChange(addonId);
  extension.sendMessage([originA]);
  let perms = await grantA;
  Assert.deepEqual(perms.added.origins, [originA], `Granted ${originA}`);

  info(`Expect ${originA} granted.`);
  await checkExpected([originA], [originA]);

  let revokeA = waitForPermissionChange(addonId);
  ExtensionPermissions.remove(addonId, { origins: [originA], permissions: [] });
  let perms2 = await revokeA;
  Assert.deepEqual(perms2.removed.origins, [originA], `Revoked ${originA}`);

  let grantB = waitForPermissionChange(addonId);
  extension.sendMessage([originB]);
  let perms3 = await grantB;
  Assert.deepEqual(perms3.added.origins, [originB], `Granted ${originB}`);

  info(`Expect ${originA} revoked and ${originB} granted.`);
  await checkExpected([originA, originB], [originB]);

  let revokeB = waitForPermissionChange(addonId);
  ExtensionPermissions.remove(addonId, { origins: [originB], permissions: [] });
  let perms4 = await revokeB;
  Assert.deepEqual(perms4.removed.origins, [originB], `Revoked ${originB}`);

  info(`Expect both origins revoked, but still present in the list.`);
  await checkExpected([originA, originB], []);

  await extension.unload();
});

add_task(async function testAllUrlsNotGrantedUnconditionally_MV3() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.manifestV3.enabled", true],
      ["extensions.originControls.grantByDefault", false],
    ],
  });

  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version: 3,
      host_permissions: ["<all_urls>"],
    },
    async background() {
      const perms = await browser.permissions.getAll();
      browser.test.sendMessage("granted-permissions", perms);
    },
  });

  await extension.startup();
  const perms = await extension.awaitMessage("granted-permissions");
  ok(
    !perms.origins.includes("<all_urls>"),
    "Optional <all_urls> should not be granted as host permission yet"
  );
  ok(
    !perms.permissions.includes("<all_urls>"),
    "Optional <all_urls> should not be granted as an API permission neither"
  );

  await extension.unload();
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_OneOfMany_AllSites_toggle() {
  // ESLint autofix will silently convert http://*/* match patterns into https.
  /* eslint-disable @microsoft/sdl/no-insecure-url */
  let id = "addon9@mochi.test";

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      name: "Test add-on 9",
      browser_specific_settings: { gecko: { id } },
      optional_permissions: ["http://*/*", "https://*/*"],
    },
    background,
    useAddonManager: "permanent",
  });
  await extension.startup();
  extension.sendMessage("init");
  await extension.awaitMessage("ready");

  // Grant the second "all sites" permission as listed in the manifest.
  await ExtensionPermissions.add("addon9@mochi.test", {
    permissions: [],
    origins: ["https://*/*"],
  });
  await extension.awaitMessage("permission-added");

  let view = await loadInitialView("extension");
  let addon = await AddonManager.getAddonByID(id);

  let card = getAddonCard(view, addon.id);

  let permsSection = card.querySelector("addon-permissions-list");
  if (!permsSection) {
    ok(!card.hasAttribute("expanded"), "The list card is not expanded");
    let loaded = waitForViewLoad(view);
    card.querySelector('[action="expand"]').click();
    await loaded;
  }

  card = getAddonCard(view, addon.id);
  let { deck, tabGroup } = card.details;

  let permsBtn = tabGroup.querySelector('[name="permissions"]');
  let permsShown = BrowserTestUtils.waitForEvent(deck, "view-changed");
  permsBtn.click();
  await permsShown;

  permsSection = card.querySelector("addon-permissions-list");
  let permission_rows = permsSection.querySelectorAll(".permission-info");
  is(permission_rows.length, 1, "Only one 'all sites' permission toggle.");

  let row = permission_rows[0];
  let toggle = row.querySelector("moz-toggle");
  ok(
    row.classList.contains("permission-info"),
    `optional permission row for "http://*/*"`
  );
  is(
    toggle.getAttribute("permission-key"),
    "http://*/*",
    `optional permission toggle exists for "http://*/*"`
  );
  ok(toggle.pressed, "Expect 'all sites' toggle to be set.");

  // Revoke the second "all sites" permission, expect toggle to be unchecked.
  await ExtensionPermissions.remove("addon9@mochi.test", {
    permissions: [],
    origins: ["https://*/*"],
  });
  await extension.awaitMessage("permission-removed");
  ok(!toggle.pressed, "Expect 'all sites' toggle not to be pressed.");

  toggle.click();

  let granted = await extension.awaitMessage("permission-added");
  Assert.deepEqual(granted, {
    permissions: [],
    origins: ["http://*/*", "https://*/*"],
  });

  await closeView(view);
  await extension.unload();
  /* eslint-enable @microsoft/sdl/no-insecure-url */
});

add_task(async function testOverrideLocalization() {
  // Mock a fluent file.
  const l10nReg = L10nRegistry.getInstance();
  const source = L10nFileSource.createMock(
    "mock",
    "app",
    ["en-US"],
    "/localization/",
    [
      {
        path: "/localization/mock.ftl",
        source: `
webext-perms-description-test-tabs = Custom description for the tabs permission
`,
      },
    ]
  );
  l10nReg.registerSources([source]);

  // Add the mocked fluent file to PERMISSION_L10N and override the tabs
  // permission to use the alternative string. In a real world use-case, this
  // would be used to add non-toolkit fluent files with permission strings of
  // APIs which are defined outside of toolkit.
  PERMISSION_L10N.addResourceIds(["mock.ftl"]);
  PERMISSION_L10N_ID_OVERRIDES.set(
    "tabs",
    "webext-perms-description-test-tabs"
  );

  let mockCleanup = () => {
    // Make sure cleanup is executed only once.
    mockCleanup = () => {};

    // Remove the non-toolkit permission string.
    PERMISSION_L10N.removeResourceIds(["mock.ftl"]);
    PERMISSION_L10N_ID_OVERRIDES.delete("tabs");
    l10nReg.removeSources(["mock"]);
  };
  registerCleanupFunction(mockCleanup);

  // Load an example add-on which uses the tabs permission.
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      manifest_version: 2,
      name: "Simple test add-on",
      browser_specific_settings: { gecko: { id: "testAddon@mochi.test" } },
      permissions: ["tabs"],
    },
    background,
    useAddonManager: "temporary",
  });
  await extension.startup();
  let addonId = extension.id;

  let win = await loadInitialView("extension");

  // Open the card and navigate to its permission list.
  let card = getAddonCard(win, addonId);
  let permsSection = card.querySelector("addon-permissions-list");
  if (!permsSection) {
    ok(!card.hasAttribute("expanded"), "The list card is not expanded");
    let loaded = waitForViewLoad(win);
    card.querySelector('[action="expand"]').click();
    await loaded;
  }

  card = getAddonCard(win, addonId);
  let { deck, tabGroup } = card.details;

  let permsBtn = tabGroup.querySelector('[name="permissions"]');
  let permsShown = BrowserTestUtils.waitForEvent(deck, "view-changed");
  permsBtn.click();
  await permsShown;
  let permissionList = card.querySelector("addon-permissions-list");
  let permissionEntries = Array.from(permissionList.querySelectorAll("li"));
  Assert.equal(
    permissionEntries.length,
    1,
    "Should find a single permission entry"
  );
  Assert.equal(
    permissionEntries[0].textContent,
    "Custom description for the tabs permission",
    "Should find the non-default permission description"
  );

  await closeView(win);
  await extension.unload();

  mockCleanup();
});

add_task(async function test_data_collection() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.dataCollectionPermissions.enabled", true]],
  });

  const TEST_CASES = [
    {
      title: "no permissions",
      data_collection_permissions: {},
      async verifyUI(card) {
        let permsSection = card.querySelector("addon-permissions-list");
        await assertVisibleSections(permsSection, []);

        let permission_rows = permsSection.querySelectorAll(".permission-info");
        is(permission_rows.length, 0, "Expected 0 permission row");

        let empty_row = permsSection.querySelector(".addon-permissions-empty");
        ok(!empty_row.hidden, "Expected empty row to be visible");
      },
    },
    {
      title: "required data collection",
      data_collection_permissions: {
        required: ["healthInfo", "locationInfo"],
      },
      async verifyUI(card) {
        let permsSection = card.querySelector("addon-permissions-list");
        await assertVisibleSections(permsSection, [
          "addon-permissions-required-data-collection",
        ]);

        let permission_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-required .permission-info"
        );
        is(permission_rows.length, 1, "Expected 1 permission row");
        is(
          permission_rows[0].textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              permissions: "health information, location",
            }
          ),
          "Expected localized permission string"
        );

        let empty_row = permsSection.querySelector(".addon-permissions-empty");
        ok(empty_row.hidden, "Expected empty row to be hidden");
      },
    },
    {
      title: "explicit no data collection",
      data_collection_permissions: {
        required: ["none"],
      },
      async verifyUI(card) {
        let permsSection = card.querySelector("addon-permissions-list");
        await assertVisibleSections(permsSection, [
          "addon-permissions-required-data-collection",
        ]);

        let permission_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-required .permission-info"
        );
        is(permission_rows.length, 1, "Expected 1 permission row");
        is(
          permission_rows[0].textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-data-none"),
          "Expected localized permission string"
        );

        let empty_row = permsSection.querySelector(".addon-permissions-empty");
        ok(empty_row.hidden, "Expected empty row to be hidden");
      },
    },
    {
      title: "optional data collection",
      data_collection_permissions: {
        optional: ["technicalAndInteraction"],
      },
      async verifyUI(card) {
        let permsSection = card.querySelector("addon-permissions-list");
        await assertVisibleSections(permsSection, [
          "addon-permissions-optional-data-collection",
        ]);

        let permission_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-optional .permission-info"
        );
        is(permission_rows.length, 1, "Expected 1 permission row");

        let toggle = permission_rows[0].querySelector("moz-toggle");
        ok(toggle, "Expected a toggle element");
        is(
          toggle.labelEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-long-technicalAndInteraction"
          ),
          "Expected localized permission string"
        );

        let empty_row = permsSection.querySelector(".addon-permissions-empty");
        ok(empty_row.hidden, "Expected empty row to be hidden");
      },
    },
    {
      title: "required and optional data collection",
      data_collection_permissions: {
        required: ["locationInfo"],
        optional: ["technicalAndInteraction"],
      },
      async verifyUI(card) {
        let permsSection = card.querySelector("addon-permissions-list");
        await assertVisibleSections(permsSection, [
          "addon-permissions-required-data-collection",
          "addon-permissions-optional-data-collection",
        ]);

        // required data collection
        let required_data_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-required .permission-info"
        );
        is(required_data_rows.length, 1, "Expected 1 permission row");
        is(
          required_data_rows[0].textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              permissions: "location",
            }
          ),
          "Expected localized permission string"
        );

        // optional data collection
        let optional_data_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-optional .permission-info"
        );
        is(optional_data_rows.length, 1, "Expected 1 permission row");
        let toggle = optional_data_rows[0].querySelector("moz-toggle");
        ok(toggle, "Expected a toggle element");
        is(
          toggle.labelEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-long-technicalAndInteraction"
          ),
          "Expected localized permission string"
        );

        // empty
        let empty_row = permsSection.querySelector(".addon-permissions-empty");
        ok(empty_row.hidden, "Expected empty row to be hidden");
      },
    },
    {
      title: "required permissions, and data collection",
      permissions: ["bookmarks"],
      data_collection_permissions: {
        required: ["none"],
        optional: ["locationInfo", "technicalAndInteraction"],
      },
      async verifyUI(card) {
        let permsSection = card.querySelector("addon-permissions-list");
        await assertVisibleSections(permsSection, [
          "addon-permissions-required",
          "addon-permissions-required-data-collection",
          "addon-permissions-optional-data-collection",
        ]);

        // required permissions
        let required_rows = permsSection.querySelectorAll(
          ".addon-permissions-required .permission-info"
        );
        is(required_rows.length, 1, "Expected 1 permission row");
        is(
          required_rows[0].textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-bookmarks"),
          "Expected localized permission string"
        );

        // required data collection
        let required_data_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-required .permission-info"
        );
        is(required_data_rows.length, 1, "Expected 1 permission row");
        is(
          required_data_rows[0].textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-data-none"),
          "Expected localized permission string"
        );

        // optional data collection
        let optional_data_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-optional .permission-info"
        );
        is(optional_data_rows.length, 2, "Expected 2 permission rows");
        let toggle = optional_data_rows[0].querySelector("moz-toggle");
        is(
          toggle.labelEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-long-locationInfo"
          ),
          "Expected localized permission string"
        );
        toggle = optional_data_rows[1].querySelector("moz-toggle");
        is(
          toggle.labelEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-long-technicalAndInteraction"
          ),
          "Expected localized permission string"
        );
      },
    },
    {
      title: "required permissions and required data collection",
      permissions: ["bookmarks"],
      data_collection_permissions: {
        required: ["bookmarksInfo"],
      },
      async verifyUI(card) {
        let permsSection = card.querySelector("addon-permissions-list");
        await assertVisibleSections(permsSection, [
          "addon-permissions-required",
          "addon-permissions-required-data-collection",
        ]);

        // required permissions
        let required_rows = permsSection.querySelectorAll(
          ".addon-permissions-required .permission-info"
        );
        is(required_rows.length, 1, "Expected 1 permission row");
        is(
          required_rows[0].textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-bookmarks"),
          "Expected localized permission string"
        );

        // required data collection
        let required_data_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-required .permission-info"
        );
        is(required_data_rows.length, 1, "Expected 1 permission row");
        is(
          required_data_rows[0].textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              permissions: "bookmarks",
            }
          ),
          "Expected localized permission string"
        );
      },
    },
    {
      title: "required and optional permissions, and data collection",
      permissions: ["bookmarks"],
      optional_permissions: ["browsingData"],
      data_collection_permissions: {
        required: ["websiteActivity"],
        optional: ["locationInfo", "technicalAndInteraction"],
      },
      async verifyUI(card) {
        let permsSection = card.querySelector("addon-permissions-list");
        await assertVisibleSections(permsSection, [
          "addon-permissions-required",
          "addon-permissions-optional",
          "addon-permissions-required-data-collection",
          "addon-permissions-optional-data-collection",
        ]);

        // required permissions
        let required_rows = permsSection.querySelectorAll(
          ".addon-permissions-required .permission-info"
        );
        is(required_rows.length, 1, "Expected 1 permission row");
        is(
          required_rows[0].textContent,
          PERMISSION_L10N.formatValueSync("webext-perms-description-bookmarks"),
          "Expected localized permission string"
        );

        // optional permissions
        let optional_rows = permsSection.querySelectorAll(
          ".addon-permissions-optional .permission-info"
        );
        is(optional_rows.length, 1, "Expected 1 permission row");
        let toggle = optional_rows[0].querySelector("moz-toggle");
        is(
          toggle.labelEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-browsingData"
          ),
          "Expected localized permission string"
        );

        // required data collection
        let required_data_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-required .permission-info"
        );
        is(required_data_rows.length, 1, "Expected 1 permission row");
        is(
          required_data_rows[0].textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-some",
            {
              permissions: "website activity",
            }
          ),
          "Expected localized permission string"
        );

        // optional data collection
        let optional_data_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-optional .permission-info"
        );
        is(optional_data_rows.length, 2, "Expected 2 permission rows");
        toggle = optional_data_rows[0].querySelector("moz-toggle");
        is(
          toggle.labelEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-long-locationInfo"
          ),
          "Expected localized permission string"
        );
        toggle = optional_data_rows[1].querySelector("moz-toggle");
        is(
          toggle.labelEl.textContent,
          PERMISSION_L10N.formatValueSync(
            "webext-perms-description-data-long-technicalAndInteraction"
          ),
          "Expected localized permission string"
        );
      },
      async togglePerms(card, extension) {
        let permsSection = card.querySelector("addon-permissions-list");
        let optional_data_rows = permsSection.querySelectorAll(
          ".addon-data-collection-permissions-optional .permission-info"
        );
        let locationToggle = optional_data_rows[0].querySelector("moz-toggle");

        let added = extension.awaitMessage("permission-added");
        locationToggle.click();
        let perms = await added;
        Assert.deepEqual(
          perms,
          {
            permissions: [],
            origins: [],
            data_collection: ["locationInfo"],
          },
          "Expected added permissions"
        );

        let removed = extension.awaitMessage("permission-removed");
        locationToggle.click();
        perms = await removed;
        Assert.deepEqual(
          perms,
          {
            permissions: [],
            origins: [],
            data_collection: ["locationInfo"],
          },
          "Expected removed permissions"
        );
      },
    },
  ];

  for (const {
    title,
    permissions,
    optional_permissions,
    data_collection_permissions,
    verifyUI,
    togglePerms,
  } of TEST_CASES) {
    info(title);

    const id = `@${title.toLowerCase().replaceAll(/[^\w]+/g, "-")}`;
    const extension = ExtensionTestUtils.loadExtension({
      manifest: {
        // Use the id as name so that we can more easily debug failures when
        // that happens.
        name: id,
        permissions,
        optional_permissions,
        browser_specific_settings: {
          gecko: {
            id,
            data_collection_permissions,
          },
        },
      },
      background,
      useAddonManager: "permanent",
    });
    await extension.startup();
    extension.sendMessage("init");
    await extension.awaitMessage("ready");

    let view = await loadInitialView("extension");
    let addon = await AddonManager.getAddonByID(id);
    let card = getAddonCard(view, addon.id);

    let permsSection = card.querySelector("addon-permissions-list");
    if (!permsSection) {
      ok(!card.hasAttribute("expanded"), "The list card is not expanded");
      let loaded = waitForViewLoad(view);
      card.querySelector('[action="expand"]').click();
      await loaded;
    }

    card = getAddonCard(view, addon.id);
    let { tabGroup } = card.details;

    let permsBtn = tabGroup.querySelector('[name="permissions"]');
    is(
      permsBtn.textContent,
      PERMISSION_L10N.formatValueSync("permissions-data-addon-button"),
      "Expected permissions and data tab"
    );

    await verifyUI(card);

    if (togglePerms) {
      await togglePerms(card, extension);
    }

    await closeView(view);
    await extension.unload();
  }

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_data_collection_and_disabled_extension() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.dataCollectionPermissions.enabled", true]],
  });

  const extensionId = "@some-id";
  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: extensionId,
          data_collection_permissions: {
            optional: ["locationInfo", "healthInfo"],
          },
        },
      },
    },
    useAddonManager: "permanent",
  });
  await extension.startup();

  let addon = await AddonManager.getAddonByID(extensionId);
  await addon.disable();
  ok(addon.userDisabled, "addon is disabled");

  let view = await loadInitialView("extension");
  let card = getAddonCard(view, addon.id);

  let permsSection = card.querySelector("addon-permissions-list");
  if (!permsSection) {
    ok(!card.hasAttribute("expanded"), "The list card is not expanded");
    let loaded = waitForViewLoad(view);
    card.querySelector('[action="expand"]').click();
    await loaded;
  }
  card = getAddonCard(view, addon.id);

  let optional_data_rows = card
    .querySelector("addon-permissions-list")
    .querySelectorAll(
      ".addon-data-collection-permissions-optional .permission-info"
    );
  let locationToggle = optional_data_rows[0].querySelector("moz-toggle");

  let permissionChangePromise = waitForPermissionChange(extensionId);
  locationToggle.click();
  let perms = await permissionChangePromise;
  Assert.deepEqual(
    perms.added,
    {
      permissions: [],
      origins: [],
      data_collection: ["locationInfo"],
    },
    "Expected permission added"
  );

  permissionChangePromise = waitForPermissionChange(extensionId);
  locationToggle.click();
  perms = await permissionChangePromise;
  Assert.deepEqual(
    perms.removed,
    {
      permissions: [],
      origins: [],
      data_collection: ["locationInfo"],
    },
    "Expected permission removed"
  );

  // Now re-enable the add-on.
  await addon.enable();
  ok(!addon.userDisabled, "Expected add-on to no longer be disabled");

  await closeView(view);
  await extension.unload();

  await SpecialPowers.popPrefEnv();
});
