"use strict";

// Tests that the default permissions are duplicated into the containers when
// the container data is loaded synchronously by ensureDataReady(), without
// ContextualIdentityService.load() ever being called.

const profileDir = do_get_profile();

const { ContextualIdentityService: cis } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs"
);

const TEST_ORIGIN = "https://example.org";
const TEST_PERMISSION = "test-permission";

function testPermission(userContextId) {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(TEST_ORIGIN),
    { userContextId }
  );
  return Services.perms.testPermissionFromPrincipal(principal, TEST_PERMISSION);
}

function assertContainersHaveDefault(message) {
  let identities = cis.getPublicIdentities();
  Assert.greater(identities.length, 0, "There are containers");

  for (let identity of identities) {
    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(identity.userContextId),
      `Container ${identity.userContextId} ${message}`
    );
  }
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

  // This also starts the permission manager before the containers are loaded,
  // as happens at startup.
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(0),
    "The default permission applies outside of any container"
  );

  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("permissions.isolateBy.userContext");
    Services.prefs.clearUserPref("permissions.manager.defaultsUrl");
  });
});

add_task(async function test_syncLoad() {
  Assert.ok(
    !(await IOUtils.exists(PathUtils.join(profileDir.path, "containers.json"))),
    "There is no containers.json, so the default containers are created"
  );

  // Loads the container data synchronously through ensureDataReady().
  assertContainersHaveDefault(
    "has the default permission after a synchronous load"
  );
});

add_task(function test_syncLoad_reimport() {
  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  assertContainersHaveDefault(
    "has the default permission after reloading the permissions"
  );
});
