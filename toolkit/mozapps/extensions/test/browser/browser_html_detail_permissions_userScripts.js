/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const { AddonTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/AddonTestUtils.sys.mjs"
);
const { PERMISSION_L10N } = ChromeUtils.importESModule(
  "resource://gre/modules/ExtensionPermissionMessages.sys.mjs"
);

AddonTestUtils.initMochitest(this);

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["extensions.userScripts.mv3.enabled", true],
      // TODO: Bug 1960273 - Update this test and remove this pref set when we
      // enable the data collection permissions on all channels.
      ["extensions.dataCollectionPermissions.enabled", false],
    ],
  });
});

function loadUserScriptsExtension(addonId) {
  return ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      manifest_version: 3,
      optional_permissions: ["tabs", "userScripts", "webNavigation"],
      browser_specific_settings: { gecko: { id: addonId } },
    },
    background() {
      browser.permissions.onAdded.addListener(perms => {
        browser.test.assertDeepEq(
          { permissions: ["userScripts"], origins: [] },
          perms,
          "permissions.onAdded for userScripts permission"
        );
        browser.test.assertTrue(!!browser.userScripts, "Has userScripts API");
        browser.test.sendMessage("onAdded");
      });
      browser.permissions.onRemoved.addListener(perms => {
        browser.test.assertDeepEq(
          { permissions: ["userScripts"], origins: [] },
          perms,
          "permissions.onRemoved for userScripts permission"
        );
        browser.test.assertTrue(!browser.userScripts, "userScripts API gone");
        browser.test.sendMessage("onRemoved");
      });
    },
  });
}

async function loadPermissionsView(addonId) {
  let view = await loadInitialView("extension");
  let loaded = waitForViewLoad(view);
  getAddonCard(view, addonId).querySelector('[action="expand"]').click();
  await loaded;

  let card = getAddonCard(view, addonId);
  let { deck, tabGroup } = card.details;

  let permsBtn = tabGroup.querySelector('[name="permissions"]');
  let permsShown = BrowserTestUtils.waitForEvent(deck, "view-changed");
  permsBtn.click();
  await permsShown;
  return view;
}

add_task(async function test_userScripts_permission_warning() {
  const addonId = "@ext_with_userScripts_permission";
  let extension = loadUserScriptsExtension(addonId);
  await extension.startup();

  let view = await loadPermissionsView(addonId);
  let card = getAddonCard(view, addonId);

  let permRows = card.querySelectorAll(
    ".addon-permissions-optional .addon-permissions-list > .permission-info"
  );

  Assert.deepEqual(
    Array.from(permRows, row =>
      row.querySelector("moz-toggle").getAttribute("permission-key")
    ),
    ["tabs", "userScripts", "webNavigation"],
    "All optional permissions displayed next to each other"
  );

  let usRow = permRows[1];
  is(
    usRow.querySelector("moz-toggle").labelEl.textContent.trim(),
    PERMISSION_L10N.formatValueSync("webext-perms-description-userScripts"),
    "userScripts permission description"
  );
  let mb = usRow.querySelector("moz-toggle > moz-message-bar[type='warning'");
  ok(mb, "Warning should be displayed after permission toggle");
  is(
    mb.messageL10nId,
    "webext-perms-extra-warning-userScripts-long",
    "Warning should be the extra description for userScripts"
  );

  Assert.deepEqual(
    [...card.querySelectorAll("addon-permissions-list moz-message-bar")],
    [mb],
    "Only userScripts should have an extra warning (not tabs nor webNavigation)"
  );

  // Now toggle the permission and confirm that the notice is still there.
  // Note that the toggling does not require any additional
  // prompts/confirmations before the permission is granted.
  ok(!usRow.querySelector("moz-toggle").pressed, "Not granted yet");
  usRow.querySelector("moz-toggle").click();
  await extension.awaitMessage("onAdded");
  ok(usRow.querySelector("moz-toggle").pressed, "userScripts granted");

  is(
    card.querySelector("addon-permissions-list moz-message-bar"),
    mb,
    "Warning is shown independently of permission toggle state"
  );

  await closeView(view);

  // Load UI from scratch, to confirm that the notice is always shown,
  // independently of whether the permission was granted / not granted.
  view = await loadPermissionsView(addonId);
  card = getAddonCard(view, addonId);
  permRows = card.querySelectorAll(
    ".addon-permissions-optional .addon-permissions-list > .permission-info"
  );
  usRow = permRows[1];
  mb = usRow.querySelector("moz-toggle > moz-message-bar[type='warning'");

  Assert.deepEqual(
    [...card.querySelectorAll("addon-permissions-list moz-message-bar")],
    [mb],
    "userScripts warning should still be shown (and be the only one)"
  );
  ok(usRow.querySelector("moz-toggle").pressed, "userScripts still granted");
  usRow.querySelector("moz-toggle").click();
  await extension.awaitMessage("onRemoved");
  ok(!usRow.querySelector("moz-toggle").pressed, "userScripts revoked");

  await closeView(view);

  await extension.unload();
});
