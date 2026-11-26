/* Any copyright is dedicated to the Public Domain.
https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PermissionTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PermissionTestUtils.sys.mjs"
);

const DOMAIN_A = "example.com";
const SUB_DOMAIN_A = "sub." + DOMAIN_A;
const DOMAIN_B = "example.org";
const DOMAIN_C = "example.net";

const ORIGIN_A = "https://" + DOMAIN_A;
const ORIGIN_SUB_A = "https://" + SUB_DOMAIN_A;
const ORIGIN_B = "https://" + DOMAIN_B;
const ORIGIN_C = "https://" + DOMAIN_C;
const ORIGIN_NON_HTTP = "file:///foo/bar.html";

const OA_PBM = { privateBrowsingId: 1 };
const PRINCIPAL_C_PBM = Services.scriptSecurityManager.createContentPrincipal(
  Services.io.newURI(ORIGIN_C),
  OA_PBM
);

const OA_CONTAINER = { userContextId: 2 };
const PRINCIPAL_B_CONTAINER =
  Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(ORIGIN_B),
    OA_CONTAINER
  );

let btp;
let userActivationLifetimeSec = Services.prefs.getIntPref(
  "privacy.bounceTrackingProtection.bounceTrackingActivationLifetimeSec"
);

function cleanup() {
  btp.clearAll();
  Services.perms.removeAll();
  Services.prefs.setBoolPref(
    "privacy.bounceTrackingProtection.hasMigratedUserActivationData",
    false
  );
}

add_setup(function () {
  Services.prefs.setIntPref(
    "privacy.bounceTrackingProtection.mode",
    Ci.nsIBounceTrackingProtection.MODE_ENABLED
  );

  btp = Cc["@mozilla.org/bounce-tracking-protection;1"].getService(
    Ci.nsIBounceTrackingProtection
  );

  // Clean initial state.
  cleanup();
});

/**
 * Adds user activation permissions, migrates them into BTP and checks which
 * user activation flags were imported.
 *
 * @param {boolean} isolateByUserContext Whether permissions are isolated by
 *   container, i.e. the value of permissions.isolateBy.userContext.
 */
async function testUserActivationPermMigration(isolateByUserContext) {
  // Assert initial test state.
  Assert.deepEqual(
    btp.testGetUserActivationHosts({}),
    [],
    "No user activation hosts initially."
  );
  Assert.equal(
    Services.perms.getAllByTypes(["storageAccessAPI"]).length,
    0,
    "No user activation permissions initially."
  );

  info("Add test user activation permissions.");

  let now = Date.now();

  // Non-expired permissions.
  PermissionTestUtils.addWithModificationTime(
    ORIGIN_A,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    now
  );
  PermissionTestUtils.addWithModificationTime(
    ORIGIN_C,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    now - 1000
  );

  // A non expired permission for a subdomain of DOMAIN_A that has an older modification time.
  PermissionTestUtils.addWithModificationTime(
    ORIGIN_SUB_A,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    now - 500
  );

  // An expired permission.
  PermissionTestUtils.addWithModificationTime(
    ORIGIN_B,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    now - userActivationLifetimeSec * 1.2 * 1000
  );

  // A non-HTTP permission.
  PermissionTestUtils.addWithModificationTime(
    ORIGIN_NON_HTTP,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    now
  );

  // A permission for PBM.
  PermissionTestUtils.addWithModificationTime(
    PRINCIPAL_C_PBM,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    now
  );

  // A non-expired permission for a container. Without isolation by container
  // it is the same as the expired permission of DOMAIN_B, which keeps its
  // modification time.
  PermissionTestUtils.addWithModificationTime(
    PRINCIPAL_B_CONTAINER,
    "storageAccessAPI",
    Services.perms.ALLOW_ACTION,
    now
  );

  info("Trigger migration.");
  btp.testMaybeMigrateUserInteractionPermissions();

  Assert.deepEqual(
    btp
      .testGetUserActivationHosts({})
      .map(entry => entry.siteHost)
      .sort(),
    [DOMAIN_A, DOMAIN_C].sort(),
    "Should have imported the correct user activation flags."
  );
  Assert.deepEqual(
    btp.testGetUserActivationHosts(OA_PBM).map(entry => entry.siteHost),
    [DOMAIN_C],
    "Should have imported the correct user activation flags for PBM."
  );
  Assert.deepEqual(
    btp.testGetUserActivationHosts(OA_CONTAINER).map(entry => entry.siteHost),
    isolateByUserContext ? [DOMAIN_B] : [],
    "Should have imported the correct user activation flags for the container."
  );

  info("Reset the BTP user activation store");
  btp.clearAll();

  info("Trigger migration again.");
  btp.testMaybeMigrateUserInteractionPermissions();

  Assert.deepEqual(
    btp.testGetUserActivationHosts({}),
    [],
    "Should not have imported the user activation flags again."
  );
  Assert.deepEqual(
    btp.testGetUserActivationHosts(OA_PBM),
    [],
    "Should not have imported the user activation flags again for PBM."
  );
  Assert.deepEqual(
    btp.testGetUserActivationHosts(OA_CONTAINER),
    [],
    "Should not have imported the user activation flags again for the container."
  );

  cleanup();
}

add_task(
  { pref_set: [["permissions.isolateBy.userContext", true]] },
  async function test_user_activation_perm_migration_isolated() {
    await testUserActivationPermMigration(true);
  }
);

add_task(
  { pref_set: [["permissions.isolateBy.userContext", false]] },
  async function test_user_activation_perm_migration_not_isolated() {
    await testUserActivationPermMigration(false);
  }
);
