/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const PERMISSIONS_FILE_NAME = "permissions.sqlite";

add_setup(function () {
  Services.prefs.setBoolPref("permissions.expireUnused.enabled", true);
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("permissions.expireUnused.enabled");
  });
});

function getPermissionsFile(profile) {
  let file = profile.clone();
  file.append(PERMISSIONS_FILE_NAME);
  return file;
}

function getInteractionCount(origin) {
  let profile = do_get_profile();
  let db = Services.storage.openDatabase(getPermissionsFile(profile));
  let stmt = db.createStatement(
    "SELECT COUNT(*) FROM moz_origin_interactions WHERE origin = :origin"
  );
  stmt.bindByName("origin", origin);
  stmt.executeStep();
  let count = stmt.getInt64(0);
  stmt.finalize();
  db.close();
  return count;
}

function getInteractionTime(origin) {
  let profile = do_get_profile();
  let db = Services.storage.openDatabase(getPermissionsFile(profile));
  let stmt = db.createStatement(
    "SELECT lastInteractionTime FROM moz_origin_interactions WHERE origin = :origin"
  );
  stmt.bindByName("origin", origin);
  let time = 0;
  if (stmt.executeStep()) {
    time = stmt.getInt64(0);
  }
  stmt.finalize();
  db.close();
  return time;
}

function setInteractionTime(origin, timeMs) {
  let profile = do_get_profile();
  let db = Services.storage.openDatabase(getPermissionsFile(profile));
  let stmt = db.createStatement(
    "INSERT OR REPLACE INTO moz_origin_interactions (origin, lastInteractionTime) VALUES (:origin, :time)"
  );
  stmt.bindByName("origin", origin);
  stmt.bindByName("time", timeMs);
  stmt.execute();
  stmt.finalize();
  db.close();
}

function getTotalInteractionCount() {
  let profile = do_get_profile();
  let db = Services.storage.openDatabase(getPermissionsFile(profile));
  let stmt = db.createStatement("SELECT COUNT(*) FROM moz_origin_interactions");
  stmt.executeStep();
  let count = stmt.getInt64(0);
  stmt.finalize();
  db.close();
  return count;
}

function hasPermission(pm, principal, type) {
  return (
    pm.testPermissionFromPrincipal(principal, type) !==
    Services.perms.UNKNOWN_ACTION
  );
}

function expiredTimestamp() {
  let thresholdMs =
    Services.prefs.getIntPref("permissions.expireUnusedThresholdSec") * 1000;
  return Date.now() - thresholdMs - 1000;
}

// Flush all pending background thread writes (including the auto-recorded
// interaction from addFromPrincipal), then overwrite with a timestamp past
// the expiry threshold.
async function writeOldInteraction(origin) {
  await Services.perms.testFlushPendingWrites();
  setInteractionTime(origin, expiredTimestamp());
}

add_task(async function test_schema_migration() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  let profile = do_get_profile();

  let pm = Services.perms;
  Assert.equal(pm.all.length, 0, "No permissions initially");

  let db = Services.storage.openDatabase(getPermissionsFile(profile));
  if (db.tableExists("moz_origin_interactions")) {
    db.executeSimpleSQL("DROP TABLE moz_origin_interactions");
  }
  db.schemaVersion = 12;
  db.close();

  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  // Accessing pm.all forces a synchronous load, triggering the migration.
  pm.all;

  db = Services.storage.openDatabase(getPermissionsFile(profile));
  Assert.ok(
    db.tableExists("moz_origin_interactions"),
    "moz_origin_interactions table should exist after migration"
  );
  Assert.equal(db.schemaVersion, 13, "Schema version should be 13");
  db.close();
});

add_task(async function test_interaction_tracking() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  pm.updateLastInteractionForPrincipal(principal);
  await pm.testFlushPendingWrites();

  Assert.greater(
    getInteractionTime("https://example.com"),
    0,
    "lastInteractionTime should be positive"
  );

  pm.removeAll();
});

add_task(
  { pref_set: [["permissions.isolateBy.userContext", false]] },
  async function test_interaction_stored_without_oa_suffix_if_not_isolated() {
    Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
    Services.prefs.setCharPref(
      "permissions.expireUnusedTypes",
      "desktop-notification"
    );

    let pm = Services.perms;
    pm.removeAll();

    let principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://oa-test.example.com^userContextId=5"
      );

    pm.addFromPrincipal(
      principal,
      "desktop-notification",
      Services.perms.ALLOW_ACTION
    );

    pm.updateLastInteractionForPrincipal(principal);
    await pm.testFlushPendingWrites();

    Assert.greater(
      getInteractionTime("https://oa-test.example.com"),
      0,
      "lastInteractionTime should be positive"
    );

    Assert.equal(
      getInteractionCount("https://oa-test.example.com^userContextId=5"),
      0,
      "Should not have a interaction record with OA suffix"
    );

    pm.removeAll();
  }
);

add_task(
  { pref_set: [["permissions.isolateBy.userContext", true]] },
  async function test_interaction_stored_with_user_context_suffix() {
    Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
    Services.prefs.setCharPref(
      "permissions.expireUnusedTypes",
      "desktop-notification"
    );

    let pm = Services.perms;
    pm.removeAll();

    let principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://uc-test.example.com^userContextId=5"
      );

    pm.addFromPrincipal(
      principal,
      "desktop-notification",
      Services.perms.ALLOW_ACTION
    );

    pm.updateLastInteractionForPrincipal(principal);
    await pm.testFlushPendingWrites();

    Assert.greater(
      getInteractionTime("https://uc-test.example.com^userContextId=5"),
      0,
      "lastInteractionTime should be positive"
    );

    Assert.equal(
      getInteractionCount("https://uc-test.example.com"),
      0,
      "Should not have an interaction record without the userContextId suffix"
    );

    pm.removeAll();
  }
);

add_task(
  async function test_interaction_tracked_without_expirable_permission() {
    Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
    Services.prefs.setCharPref("permissions.expireUnusedTypes", "geo");

    let pm = Services.perms;
    pm.removeAll();

    let principal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://nopermission.example.com"
      );

    pm.updateLastInteractionForPrincipal(principal);
    await pm.testFlushPendingWrites();

    Assert.equal(
      getInteractionCount("https://nopermission.example.com"),
      1,
      "Should have an interaction record even without expirable permission"
    );
  }
);

add_task(async function test_default_permission_not_expired() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://default.example.com"
    );

  pm.addDefaultFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  setInteractionTime("https://default.example.com", expiredTimestamp());

  Services.obs.notifyObservers(null, "idle-daily");

  await new Promise(resolve => executeSoon(resolve));

  Assert.ok(
    hasPermission(pm, principal, "desktop-notification"),
    "Default permission should not be expired"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_policy_permission_not_expired() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://policy.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION,
    Services.perms.EXPIRE_POLICY
  );

  await writeOldInteraction("https://policy.example.com");

  Services.obs.notifyObservers(null, "idle-daily");

  await new Promise(resolve => executeSoon(resolve));

  Assert.ok(
    hasPermission(pm, principal, "desktop-notification"),
    "Policy permission should not be expired"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_permission_not_in_expirable_types_not_expired() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref("permissions.expireUnusedTypes", "geo");
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://notinlist.example.com"
    );

  pm.addFromPrincipal(principal, "camera", Services.perms.ALLOW_ACTION);

  await writeOldInteraction("https://notinlist.example.com");

  Services.obs.notifyObservers(null, "idle-daily");

  await new Promise(resolve => executeSoon(resolve));

  Assert.ok(
    hasPermission(pm, principal, "camera"),
    "Permission not in expirable types should not be expired"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_recent_interaction_prevents_expiration() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 86400);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://recentvisit.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  pm.updateLastInteractionForPrincipal(principal);

  Services.obs.notifyObservers(null, "idle-daily");

  await new Promise(resolve => executeSoon(resolve));

  Assert.ok(
    hasPermission(pm, principal, "desktop-notification"),
    "Permission with recent interaction should not be expired"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_old_interaction_allows_expiration() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://oldvisit.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  await writeOldInteraction("https://oldvisit.example.com");

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () => !hasPermission(pm, principal, "desktop-notification"),
    "Waiting for permission to be expired"
  );

  Assert.ok(
    !hasPermission(pm, principal, "desktop-notification"),
    "Permission with old interaction should be expired"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_permission_grant_records_interaction() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://novisit.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  await pm.testFlushPendingWrites();

  Assert.equal(
    getInteractionCount("https://novisit.example.com"),
    1,
    "Should have an interaction record from permission grant"
  );

  // Overwrite with old timestamp so it's eligible for expiry.
  setInteractionTime("https://novisit.example.com", expiredTimestamp());

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () => !hasPermission(pm, principal, "desktop-notification"),
    "Waiting for permission to be expired"
  );

  Assert.ok(
    !hasPermission(pm, principal, "desktop-notification"),
    "Permission should be expired after threshold exceeded"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_glean_telemetry_on_expiration() {
  Services.fog.testResetFOG();

  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://telemetry.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  await writeOldInteraction("https://telemetry.example.com");

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () => !hasPermission(pm, principal, "desktop-notification"),
    "Waiting for permission to be expired"
  );

  let byType = Glean.permissions.unusedPermissionsExpiredByType.testGetValue();
  Assert.ok(byType, "Should have by-type data");
  Assert.greaterOrEqual(
    byType["desktop-notification"],
    1,
    "Should record desktop-notification type"
  );

  let ageDist = Glean.permissions.unusedPermissionAgeAtExpiry.testGetValue();
  Assert.ok(ageDist, "Should have age distribution data");

  let modAgeDist =
    Glean.permissions.unusedPermissionModifiedAgeAtExpiry.testGetValue();
  Assert.ok(modAgeDist, "Should have modified age distribution data");

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_no_telemetry_when_disabled() {
  Services.fog.testResetFOG();

  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);
  Services.prefs.setBoolPref("permissions.expireUnused.enabled", false);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://disabled.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  setInteractionTime("https://disabled.example.com", expiredTimestamp());

  Services.obs.notifyObservers(null, "idle-daily");

  await new Promise(resolve => executeSoon(resolve));

  Assert.ok(
    hasPermission(pm, principal, "desktop-notification"),
    "Permission should not be expired when feature is disabled"
  );

  let byType = Glean.permissions.unusedPermissionsExpiredByType.testGetValue();
  Assert.ok(
    !byType || !Object.keys(byType).length,
    "Should not record by-type telemetry when disabled"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
  Services.prefs.setBoolPref("permissions.expireUnused.enabled", true);
});

add_task(async function test_glean_by_type_multiple_types() {
  Services.fog.testResetFOG();

  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification,geo"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal1 =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://bytype1.example.com"
    );
  let principal2 =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://bytype2.example.com"
    );

  pm.addFromPrincipal(
    principal1,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  pm.addFromPrincipal(principal2, "geo", Services.perms.ALLOW_ACTION);

  await writeOldInteraction("https://bytype1.example.com");
  await writeOldInteraction("https://bytype2.example.com");

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () =>
      !hasPermission(pm, principal1, "desktop-notification") &&
      !hasPermission(pm, principal2, "geo"),
    "Waiting for both permissions to be expired"
  );

  let byType = Glean.permissions.unusedPermissionsExpiredByType.testGetValue();
  Assert.ok(byType, "Should have by-type data");
  Assert.equal(
    byType["desktop-notification"],
    1,
    "Should record one desktop-notification expiration"
  );
  Assert.equal(byType.geo, 1, "Should record one geo expiration");

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

// Interaction tracking always runs regardless of the expireUnused pref,
// so that data is ready when the feature is later enabled.
add_task(async function test_interaction_tracked_even_when_disabled() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setBoolPref("permissions.expireUnused.enabled", false);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://visitdisabled.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  pm.updateLastInteractionForPrincipal(principal);
  await pm.testFlushPendingWrites();

  Assert.equal(
    getInteractionCount("https://visitdisabled.example.com"),
    1,
    "Should have an interaction record even when feature is disabled"
  );

  pm.removeAll();
  Services.prefs.setBoolPref("permissions.expireUnused.enabled", true);
});

add_task(async function test_double_keyed_permission_expiration() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "open-protocol-handler^"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://doublekey.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "open-protocol-handler^zoommtg",
    Services.perms.ALLOW_ACTION
  );
  pm.addFromPrincipal(principal, "geo", Services.perms.ALLOW_ACTION);

  Assert.ok(
    hasPermission(pm, principal, "open-protocol-handler^zoommtg"),
    "Double-keyed permission should exist"
  );
  Assert.ok(hasPermission(pm, principal, "geo"), "Geo permission should exist");

  await writeOldInteraction("https://doublekey.example.com");

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () => !hasPermission(pm, principal, "open-protocol-handler^zoommtg"),
    "Waiting for double-keyed permission to be expired"
  );

  Assert.ok(
    hasPermission(pm, principal, "geo"),
    "Geo permission should NOT be expired (not in expirable types)"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_double_keyed_vs_regular_permission() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref("permissions.expireUnusedTypes", "testperm^");
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://testperm.example.com"
    );

  pm.addFromPrincipal(principal, "testperm", Services.perms.ALLOW_ACTION);
  pm.addFromPrincipal(principal, "testperm^key", Services.perms.ALLOW_ACTION);

  await writeOldInteraction("https://testperm.example.com");

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () => !hasPermission(pm, principal, "testperm^key"),
    "Waiting for double-keyed permission to be expired"
  );

  Assert.ok(
    hasPermission(pm, principal, "testperm"),
    "Regular permission should NOT be expired"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_oa_isolated_permission_expiration() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref("permissions.expireUnusedTypes", "geo,cookie");
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);
  Services.prefs.setBoolPref("permissions.isolateBy.userContext", true);

  let pm = Services.perms;
  pm.removeAll();

  let principalWithOA =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://oatest.example.com^userContextId=3"
    );

  pm.addFromPrincipal(principalWithOA, "geo", Services.perms.ALLOW_ACTION);
  pm.addFromPrincipal(principalWithOA, "cookie", Services.perms.ALLOW_ACTION);

  Assert.ok(
    hasPermission(pm, principalWithOA, "geo"),
    "Geo permission should exist"
  );
  Assert.ok(
    hasPermission(pm, principalWithOA, "cookie"),
    "Cookie permission should exist"
  );

  // Flush auto-recorded interactions, then overwrite OA interaction
  // with an old timestamp so it's eligible for expiry.
  await pm.testFlushPendingWrites();

  setInteractionTime(
    "https://oatest.example.com^userContextId=3",
    expiredTimestamp()
  );

  // Add a fresh interaction for the base origin only (without OA suffix).
  setInteractionTime("https://oatest.example.com", Date.now());

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () => !hasPermission(pm, principalWithOA, "geo"),
    "Waiting for OA-isolated geo permission to be expired"
  );

  Assert.ok(
    hasPermission(pm, principalWithOA, "cookie"),
    "OA-stripped permission (cookie) should survive via base origin interaction"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
  Services.prefs.clearUserPref("permissions.isolateBy.userContext");
});

add_task(
  async function test_oa_isolated_permission_with_matching_interaction() {
    Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
    Services.prefs.setCharPref("permissions.expireUnusedTypes", "geo");
    Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);
    Services.prefs.setBoolPref("permissions.isolateBy.userContext", true);

    let pm = Services.perms;
    pm.removeAll();

    let principalWithOA =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://oamatch.example.com^userContextId=7"
      );

    pm.addFromPrincipal(principalWithOA, "geo", Services.perms.ALLOW_ACTION);

    await writeOldInteraction("https://oamatch.example.com^userContextId=7");

    Services.obs.notifyObservers(null, "idle-daily");

    await TestUtils.waitForCondition(
      () => !hasPermission(pm, principalWithOA, "geo"),
      "Waiting for OA-isolated permission to be expired"
    );

    Assert.ok(
      !hasPermission(pm, principalWithOA, "geo"),
      "OA-isolated permission should be expired when matching OA interaction exists"
    );

    pm.removeAll();
    Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
    Services.prefs.clearUserPref("permissions.isolateBy.userContext");
  }
);

add_task(async function test_no_interaction_tracking_for_private_browsing() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );

  let pm = Services.perms;
  pm.removeAll();

  let privatePrincipal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://private.example.com^privateBrowsingId=1"
    );

  pm.addFromPrincipal(
    privatePrincipal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  pm.updateLastInteractionForPrincipal(privatePrincipal);
  await pm.testFlushPendingWrites();

  let profile = do_get_profile();
  let db = Services.storage.openDatabase(getPermissionsFile(profile));
  let stmt = db.createStatement(
    "SELECT COUNT(*) FROM moz_origin_interactions WHERE origin LIKE :pattern"
  );
  stmt.bindByName("pattern", "https://private.example.com%");
  stmt.executeStep();

  Assert.equal(
    stmt.getInt64(0),
    0,
    "Should not have any interaction record for private browsing principal"
  );

  stmt.finalize();
  db.close();

  pm.removeAll();
});

add_task(async function test_restart_does_not_reset_interaction_time() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://restart-test.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  await pm.testFlushPendingWrites();

  let originalTime = getInteractionTime("https://restart-test.example.com");

  Assert.greater(originalTime, 0, "Should have interaction time recorded");

  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  await new Promise(resolve => executeSoon(resolve));

  let timeAfterReload = getInteractionTime("https://restart-test.example.com");

  Assert.equal(
    timeAfterReload,
    originalTime,
    "Interaction time should not change after reloading from disk"
  );

  pm.removeAll();
});

add_task(async function test_migration_seeds_interaction_records() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");

  let pm = Services.perms;
  pm.removeAll();

  let principal1 =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://migration1.example.com"
    );
  let principal2 =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://migration2.example.com"
    );

  pm.addFromPrincipal(
    principal1,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  pm.addFromPrincipal(principal2, "geo", Services.perms.ALLOW_ACTION);

  await pm.testFlushPendingWrites();

  let profile = do_get_profile();
  let db = Services.storage.openDatabase(getPermissionsFile(profile));
  db.executeSimpleSQL("DROP TABLE moz_origin_interactions");
  db.schemaVersion = 12;
  db.close();

  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  // The migration runs on the DB thread; flush to ensure it completes.
  await pm.testFlushPendingWrites();

  Assert.greater(
    getInteractionCount("https://migration1.example.com"),
    0,
    "Migration should seed interaction for origin 1"
  );
  Assert.greater(
    getInteractionCount("https://migration2.example.com"),
    0,
    "Migration should seed interaction for origin 2"
  );

  let time1 = getInteractionTime("https://migration1.example.com");
  let time2 = getInteractionTime("https://migration2.example.com");

  Assert.greater(time1, 0, "Interaction time 1 should be positive");
  Assert.greater(time2, 0, "Interaction time 2 should be positive");

  pm.removeAll();
});

add_task(
  async function test_oa_stripped_permission_cleanup_preserves_oa_interactions() {
    Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
    Services.prefs.setCharPref("permissions.expireUnusedTypes", "cookie");
    Services.prefs.setBoolPref("permissions.isolateBy.userContext", true);

    let pm = Services.perms;
    pm.removeAll();

    let principalWithOA =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://cleanup-oa.example.com^userContextId=4"
      );

    pm.addFromPrincipal(principalWithOA, "cookie", Services.perms.ALLOW_ACTION);

    await pm.testFlushPendingWrites();

    Assert.equal(
      getInteractionCount("https://cleanup-oa.example.com"),
      1,
      "Should have base origin interaction for OA-stripped permission"
    );

    // Simulate IPC interaction recording with full OA
    setInteractionTime(
      "https://cleanup-oa.example.com^userContextId=4",
      Date.now()
    );

    Assert.equal(
      getInteractionCount("https://cleanup-oa.example.com^userContextId=4"),
      1,
      "Should have full-OA interaction record"
    );

    // Add another permission to trigger cleanup
    let otherPrincipal =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://other.example.com"
      );
    pm.addFromPrincipal(otherPrincipal, "cookie", Services.perms.ALLOW_ACTION);

    await pm.testFlushPendingWrites();

    pm.removeFromPrincipal(otherPrincipal, "cookie");
    await pm.removeOrphanedInteractionRecords();

    Assert.equal(
      getInteractionCount("https://cleanup-oa.example.com^userContextId=4"),
      1,
      "Full-OA interaction should survive cleanup for OA-stripped permission"
    );

    Assert.equal(
      getInteractionCount("https://cleanup-oa.example.com"),
      1,
      "Base origin interaction should survive cleanup"
    );

    pm.removeAll();
    Services.prefs.clearUserPref("permissions.isolateBy.userContext");
  }
);

add_task(async function test_removeAll_clears_interactions() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );

  let pm = Services.perms;
  pm.removeAll();

  let principal1 =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://removeall1.example.com"
    );
  let principal2 =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://removeall2.example.com"
    );

  pm.addFromPrincipal(
    principal1,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  pm.addFromPrincipal(
    principal2,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  await pm.testFlushPendingWrites();

  Assert.greaterOrEqual(
    getTotalInteractionCount(),
    2,
    "Should have at least 2 interaction records"
  );

  pm.removeAll();
  await pm.testFlushPendingWrites();

  Assert.equal(
    getTotalInteractionCount(),
    0,
    "All interaction records should be cleared after removeAll"
  );
});

add_task(
  async function test_removeAllExceptTypes_clears_orphaned_interactions() {
    Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
    Services.prefs.setCharPref(
      "permissions.expireUnusedTypes",
      "desktop-notification"
    );

    let pm = Services.perms;
    pm.removeAll();

    let principal1 =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://removeexcept1.example.com"
      );
    let principal2 =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://removeexcept2.example.com"
      );

    pm.addFromPrincipal(
      principal1,
      "desktop-notification",
      Services.perms.ALLOW_ACTION
    );
    pm.addFromPrincipal(principal2, "cookie", Services.perms.ALLOW_ACTION);

    await pm.testFlushPendingWrites();

    pm.removeAllExceptTypes(["cookie"]);
    await pm.testFlushPendingWrites();

    Assert.equal(
      getInteractionCount("https://removeexcept1.example.com"),
      0,
      "Interaction for removed desktop-notification permission should be gone"
    );

    Assert.ok(
      hasPermission(pm, principal2, "cookie"),
      "Cookie permission should be kept"
    );

    pm.removeAll();
  }
);

add_task(
  async function test_removeAllSinceWithTypeExceptions_clears_orphaned_interactions() {
    Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
    Services.prefs.setCharPref(
      "permissions.expireUnusedTypes",
      "desktop-notification"
    );

    let pm = Services.perms;
    pm.removeAll();

    let principal1 =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://removesince1.example.com"
      );
    let principal2 =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://removesince2.example.com"
      );

    pm.addFromPrincipal(
      principal1,
      "desktop-notification",
      Services.perms.ALLOW_ACTION
    );

    await pm.testFlushPendingWrites();

    let perm1 = pm.getPermissionObject(
      principal1,
      "desktop-notification",
      true
    );
    let midTime = perm1.modificationTime + 1;

    // Ensure PR_Now() advances past midTime before adding the next permission.
    // Windows timer resolution can be ~16ms, so we need a real delay.
    await new Promise(resolve => do_timeout(20, resolve));

    pm.addFromPrincipal(
      principal2,
      "desktop-notification",
      Services.perms.ALLOW_ACTION
    );
    await pm.testFlushPendingWrites();

    pm.removeAllSinceWithTypeExceptions(midTime, []);
    await pm.testFlushPendingWrites();

    Assert.equal(
      getInteractionCount("https://removesince2.example.com"),
      0,
      "Interaction for removed recent permission should be gone"
    );

    Assert.greater(
      getInteractionCount("https://removesince1.example.com"),
      0,
      "Interaction for kept older permission should remain"
    );

    pm.removeAll();
  }
);

add_task(
  async function test_removePermissionsWithAttributes_clears_orphaned_interactions() {
    Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
    Services.prefs.setCharPref(
      "permissions.expireUnusedTypes",
      "desktop-notification"
    );
    Services.prefs.setBoolPref("permissions.isolateBy.userContext", true);

    let pm = Services.perms;
    pm.removeAll();

    let principal1 =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://removeoa1.example.com^userContextId=1"
      );
    let principal2 =
      Services.scriptSecurityManager.createContentPrincipalFromOrigin(
        "https://removeoa2.example.com"
      );

    pm.addFromPrincipal(
      principal1,
      "desktop-notification",
      Services.perms.ALLOW_ACTION
    );
    pm.addFromPrincipal(
      principal2,
      "desktop-notification",
      Services.perms.ALLOW_ACTION
    );

    await pm.testFlushPendingWrites();

    pm.removePermissionsWithAttributes('{"userContextId":1}', [], []);
    await pm.testFlushPendingWrites();

    Assert.ok(
      !hasPermission(pm, principal1, "desktop-notification"),
      "Permission with userContextId=1 should be removed"
    );

    Assert.equal(
      getInteractionCount("https://removeoa1.example.com^userContextId=1"),
      0,
      "Interaction for removed permission should be gone"
    );

    Assert.greater(
      getInteractionCount("https://removeoa2.example.com"),
      0,
      "Interaction for the origin with remaining permission should remain"
    );

    pm.removeAll();
    Services.prefs.clearUserPref("permissions.isolateBy.userContext");
  }
);

add_task(async function test_removePermission_clears_interactions() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );

  let pm = Services.perms;
  pm.removeAll();

  let principal1 =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://removeperm1.example.com"
    );
  let principal2 =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://removeperm2.example.com"
    );

  pm.addFromPrincipal(
    principal1,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );
  pm.addFromPrincipal(
    principal2,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  await pm.testFlushPendingWrites();

  Assert.equal(
    getInteractionCount("https://removeperm1.example.com"),
    1,
    "Should have interaction for first origin"
  );
  Assert.equal(
    getInteractionCount("https://removeperm2.example.com"),
    1,
    "Should have interaction for second origin"
  );

  let perm = pm.getPermissionObject(principal1, "desktop-notification", true);
  pm.removePermission(perm);
  await pm.removeOrphanedInteractionRecords();

  Assert.equal(
    getInteractionCount("https://removeperm1.example.com"),
    0,
    "Interaction for removed permission should be gone"
  );

  Assert.equal(
    getInteractionCount("https://removeperm2.example.com"),
    1,
    "Interaction for remaining permission should still exist"
  );

  pm.removeAll();
});

add_task(async function test_idle_daily_cleans_orphaned_interactions() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://idle-cleanup.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  await pm.testFlushPendingWrites();

  pm.removeFromPrincipal(principal, "desktop-notification");

  Assert.greater(
    getInteractionCount("https://idle-cleanup.example.com"),
    0,
    "Interaction should still exist after removeFromPrincipal"
  );

  Services.obs.notifyObservers(null, "idle-daily");

  await TestUtils.waitForCondition(
    () => getInteractionCount("https://idle-cleanup.example.com") === 0,
    "Orphaned interaction should be cleaned up by idle-daily"
  );

  pm.removeAll();
});

add_task(async function test_session_permission_not_expired() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://session.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION,
    Services.perms.EXPIRE_SESSION
  );

  await writeOldInteraction("https://session.example.com");

  Assert.ok(
    hasPermission(pm, principal, "desktop-notification"),
    "Session permission should exist before idle-daily"
  );

  Services.obs.notifyObservers(null, "idle-daily");

  await new Promise(resolve => executeSoon(resolve));

  Assert.ok(
    hasPermission(pm, principal, "desktop-notification"),
    "Session permission should not be expired by unused-permissions logic"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_private_browsing_permission_not_expired() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let privatePrincipal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://pbm-expire.example.com^privateBrowsingId=1"
    );

  // Use addFromPrincipalAndPersistInPrivateBrowsing to create a PBM
  // permission with EXPIRE_NEVER (bypasses the SESSION override).
  pm.addFromPrincipalAndPersistInPrivateBrowsing(
    privatePrincipal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  Assert.ok(
    hasPermission(pm, privatePrincipal, "desktop-notification"),
    "PBM-persisted permission should exist"
  );

  // Manually seed an old interaction so it would be eligible for expiry
  // if the PBM guard weren't in place.
  setInteractionTime(
    "https://pbm-expire.example.com^privateBrowsingId=1",
    expiredTimestamp()
  );
  setInteractionTime("https://pbm-expire.example.com", expiredTimestamp());

  Services.obs.notifyObservers(null, "idle-daily");

  await new Promise(resolve => executeSoon(resolve));

  Assert.ok(
    hasPermission(pm, privatePrincipal, "desktop-notification"),
    "PBM permission should not be expired"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});

add_task(async function test_no_interaction_record_keeps_permission() {
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  Services.prefs.setCharPref(
    "permissions.expireUnusedTypes",
    "desktop-notification"
  );
  Services.prefs.setIntPref("permissions.expireUnusedThresholdSec", 1);

  let pm = Services.perms;
  pm.removeAll();

  let principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://no-interaction.example.com"
    );

  pm.addFromPrincipal(
    principal,
    "desktop-notification",
    Services.perms.ALLOW_ACTION
  );

  // Flush the auto-recorded interaction, then delete it so the
  // permission has no interaction record at all.
  await pm.testFlushPendingWrites();

  let profile = do_get_profile();
  let db = Services.storage.openDatabase(getPermissionsFile(profile));
  db.executeSimpleSQL(
    "DELETE FROM moz_origin_interactions WHERE origin = 'https://no-interaction.example.com'"
  );
  db.close();

  Assert.equal(
    getInteractionCount("https://no-interaction.example.com"),
    0,
    "Interaction record should be deleted"
  );

  Services.obs.notifyObservers(null, "idle-daily");

  await new Promise(resolve => executeSoon(resolve));

  Assert.ok(
    hasPermission(pm, principal, "desktop-notification"),
    "Permission with no interaction record should be kept (not expired)"
  );

  pm.removeAll();
  Services.prefs.clearUserPref("permissions.expireUnusedThresholdSec");
});
