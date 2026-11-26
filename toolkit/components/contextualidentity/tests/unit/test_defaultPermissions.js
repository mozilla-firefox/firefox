"use strict";

// Tests that the default permissions are duplicated into the containers, since
// permissions are isolated by container.

const profileDir = do_get_profile();

// The permission manager looks the containers up through the
// nsISiteContainerService singleton, so this has to drive that one rather than
// an instance of its own.
const { ContextualIdentityService: cis } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
);

const TEST_ORIGIN = "https://example.org";
// Origin of the default permission which is added while the test is running.
const LATE_TEST_ORIGIN = "https://example.com";
const TEST_PERMISSION = "test-permission";

function testPermission(userContextId, origin = TEST_ORIGIN) {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(origin),
    { userContextId }
  );
  return Services.perms.testPermissionFromPrincipal(principal, TEST_PERMISSION);
}

add_setup(async function setup() {
  Services.prefs.setBoolPref("permissions.isolateBy.userContext", true);

  let path = PathUtils.join(profileDir.path, "test-default-permissions");
  await IOUtils.writeUTF8(
    path,
    `origin\t${TEST_PERMISSION}\t1\t${TEST_ORIGIN}\n`
  );

  Services.prefs.setCharPref(
    "permissions.manager.defaultsUrl",
    PathUtils.toFileURI(path)
  );
  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  // This also starts the permission manager, which has to be running to
  // observe the containers being loaded.
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(0),
    "The default permission applies outside of any container"
  );

  await cis.load();

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("permissions.isolateBy.userContext");
    Services.prefs.clearUserPref("permissions.manager.defaultsUrl");
  });
});

add_task(function test_defaultPermissions() {
  let identities = cis.getPublicIdentities();
  Assert.greater(identities.length, 0, "There are containers");

  for (let identity of identities) {
    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(identity.userContextId),
      `Container ${identity.userContextId} has the default permission`
    );
  }

  let identity = cis.create("Test", "icon", "blue");
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(identity.userContextId),
    "A newly created container has the default permission"
  );

  let policyIdentity = cis.createForPolicy("test-policy");
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(policyIdentity.userContextId),
    "A newly created policy container has the default permission"
  );
});

// Default permissions which are added later are only duplicated into the
// containers which still exist.
add_task(function test_removedContainer() {
  let removed = cis.create("Removed", "icon", "blue");
  let kept = cis.create("Kept", "icon", "blue");
  Assert.ok(cis.remove(removed.userContextId), "The container was removed");
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(removed.userContextId),
    "The removed container doesn't keep its copy of the default permission"
  );

  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(LATE_TEST_ORIGIN),
    {}
  );
  Services.perms.addDefaultFromPrincipal(
    principal,
    TEST_PERMISSION,
    Ci.nsIPermissionManager.ALLOW_ACTION
  );

  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(kept.userContextId, LATE_TEST_ORIGIN),
    "A container which still exists has the new default permission"
  );
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(removed.userContextId, LATE_TEST_ORIGIN),
    "The removed container doesn't have the new default permission"
  );
});

add_task(function test_removedPolicyContainer() {
  let identity = cis.createForPolicy("removed-policy");
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(identity.userContextId),
    "The policy container has the default permission"
  );

  Assert.ok(
    cis.removePolicyIdentity(identity.userContextId),
    "The policy container was removed"
  );
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(identity.userContextId),
    "The removed policy container doesn't keep its copy of the default permission"
  );
});

// Disabling the containers resets them to the default ones, which deletes the
// others and recreates the deleted default ones.
add_task(async function test_disableContainers() {
  let identity = cis.create("Reset", "icon", "blue");
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(identity.userContextId),
    "The container has the default permission"
  );
  let policyIdentity = cis.createForPolicy("reset-policy");
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(policyIdentity.userContextId),
    "The policy container has the default permission"
  );
  let removedDefault = cis.getPublicIdentities()[0].userContextId;
  Assert.ok(cis.remove(removedDefault), "A default container was removed");
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(removedDefault),
    "The removed default container doesn't keep the default permission"
  );

  using cleanup = new DisposableStack();
  Services.prefs.setBoolPref("privacy.userContext.enabled", false);
  cleanup.defer(() =>
    Services.prefs.clearUserPref("privacy.userContext.enabled")
  );
  await cis.observe(null, "nsPref:changed");

  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(identity.userContextId),
    "The deleted container doesn't keep its copy of the default permission"
  );
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(policyIdentity.userContextId),
    "The deleted policy container doesn't keep its copy of the default permission"
  );
  for (let defaultIdentity of cis.getPublicIdentities()) {
    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(defaultIdentity.userContextId),
      `Default container ${defaultIdentity.userContextId} has the default permission`
    );
  }
});
