"use strict";

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const PERMISSIONS_URL =
  "chrome://browser/content/preferences/dialogs/permissions.xhtml";

let sitePermissionsDialog;

let principal = Services.scriptSecurityManager.createContentPrincipal(
  Services.io.newURI("http://www.example.com"),
  {}
);
let pbPrincipal = Services.scriptSecurityManager.principalWithOA(principal, {
  privateBrowsingId: 1,
});
let principalB = Services.scriptSecurityManager.createContentPrincipal(
  Services.io.newURI("https://example.org"),
  {}
);

/**
 * Replaces the default permissions defined in browser/app/permissions with our
 * own test-only permissions and instructs the permission manager to import
 * them.
 */
async function addDefaultTestPermissions() {
  // create a file in the temp directory with the defaults.
  let file = Services.dirsvc.get("TmpD", Ci.nsIFile);
  file.append("test_default_permissions");

  await IOUtils.writeUTF8(
    file.path,
    `origin\tinstall\t1\t${principal.origin}\norigin\tinstall\t1\t${pbPrincipal.origin}\n`
  );

  // Change the default permission file path.
  await SpecialPowers.pushPrefEnv({
    set: [
      ["permissions.manager.defaultsUrl", Services.io.newFileURI(file).spec],
    ],
  });

  // Call the permission manager to reload default permissions from file.
  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  registerCleanupFunction(async () => {
    // Clean up temporary default permission file.
    await IOUtils.remove(file.path);

    // Restore non-test default permissions.
    await SpecialPowers.popPrefEnv();
    Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");
  });
}

async function openPermissionsDialog() {
  let dialogOpened = promiseLoadSubDialog(PERMISSIONS_URL);

  await SpecialPowers.spawn(gBrowser.selectedBrowser, [], function () {
    let doc = content.document;
    let settingsButton = doc.getElementById("addonExceptions");
    settingsButton.click();
  });

  sitePermissionsDialog = await dialogOpened;
  await sitePermissionsDialog.document.mozSubdialogReady;
}

add_setup(async function () {
  await addDefaultTestPermissions();
});

/**
 * Tests that default install permissions can be removed, and that the default
 * for private browsing shares the entry of the default for the normal context.
 */
add_task(async function removeAll() {
  await openPermissionsPane({ leaveOpen: true });
  await openPermissionsDialog();

  let doc = sitePermissionsDialog.document;
  let richlistbox = doc.getElementById("permissionsBox");

  Assert.equal(
    richlistbox.itemCount,
    1,
    "Both defaults should share a single entry."
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(pbPrincipal, "install")?.principal
      .origin,
    principal.origin,
    "The permission should be stored without origin attributes."
  );

  info("Adding a new non-default install permission");
  PermissionTestUtils.add(principalB, "install", Services.perms.ALLOW_ACTION);

  info("Waiting for the permission to appear in the list.");
  await BrowserTestUtils.waitForMutationCondition(
    richlistbox,
    { childList: true },
    () => richlistbox.itemCount == 2
  );

  info("Clicking remove all.");
  let removeAllButton = doc.getElementById("removeAllPermissions");
  await removeAllButton.updateComplete;
  removeAllButton.click();

  info("Waiting for all list items to be cleared.");
  await BrowserTestUtils.waitForMutationCondition(
    richlistbox,
    { childList: true },
    () => richlistbox.itemCount == 0
  );

  let dialogClosePromise = BrowserTestUtils.waitForEvent(
    sitePermissionsDialog,
    "dialogclosing",
    true
  );

  info("Accepting dialog to apply the changes.");
  doc.querySelector("dialog").getButton("accept").click();

  info("Waiting for dialog to close.");
  await dialogClosePromise;

  info("Waiting for all permissions to be removed.");
  await TestUtils.waitForCondition(
    () =>
      PermissionTestUtils.getPermissionObject(principal, "install") == null &&
      PermissionTestUtils.getPermissionObject(pbPrincipal, "install") == null &&
      PermissionTestUtils.getPermissionObject(principalB, "install") == null
  );

  info("Opening the permissions dialog again.");
  await openPermissionsDialog();

  Assert.equal(
    richlistbox.itemCount,
    0,
    "Permission list should still be empty."
  );

  // Cleanup
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
  Services.perms.removeAll();
});
