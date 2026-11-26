/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

// Tests that the default permissions are duplicated into containers

const { MockRegistrar } = ChromeUtils.importESModule(
  "resource://testing-common/MockRegistrar.sys.mjs"
);

const TEST_ORIGIN = "https://example.org";
const TEST_ORIGIN_2 = "https://example.com";
const TEST_PERMISSION = "test-permission";
// A permission type which is never isolated by origin attributes, see
// kStripOAPermissions.
const STRIPPED_PERMISSION = "cookie";

function principalForAttrs(attrs) {
  return Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(TEST_ORIGIN),
    attrs
  );
}

function testPermissionForAttrs(attrs, type = TEST_PERMISSION) {
  return Services.perms.testPermissionFromPrincipal(
    principalForAttrs(attrs),
    type
  );
}

function testPermission(userContextId, type = TEST_PERMISSION) {
  return testPermissionForAttrs({ userContextId }, type);
}

function revokeForAttrs(attrs) {
  Services.perms.removeFromPrincipal(principalForAttrs(attrs), TEST_PERMISSION);
}

function countPermissions(type) {
  return Services.perms.all.filter(perm => perm.type == type).length;
}

function reloadDefaults() {
  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");
}

// Mock nsISiteContainerService to control which containers exist
const SITE_CONTAINER_SERVICE_CONTRACT_ID =
  "@mozilla.org/site-container-service;1";
let gUserContextIds = [];

const gSiteContainerService = {
  QueryInterface: ChromeUtils.generateQI(["nsISiteContainerService"]),
  lookup: () => 0,
  containerForNavigation: (uri, baselineUserContextId) => baselineUserContextId,
  getPublicUserContextIds: () => gUserContextIds,
  getPrivateUserContextIds: () => [],
};

// Makes the containers exist and notifies about them the way
// ContextualIdentityService does when it creates one.
function addContainers(userContextIds) {
  for (let userContextId of userContextIds) {
    if (!gUserContextIds.includes(userContextId)) {
      gUserContextIds.push(userContextId);
    }
    Services.obs.notifyObservers(
      null,
      "contextual-identity-created",
      String(userContextId)
    );
  }
}

// Simulates a restart. Pending writes have to be flushed first, otherwise the
// database may be reread before it has the permissions being tested.
async function restart() {
  await Services.perms.testFlushPendingWrites();
  reloadDefaults();
}

add_setup(async function setup() {
  do_get_profile();

  Services.prefs.setBoolPref("permissions.isolateBy.userContext", true);
  Services.prefs.setBoolPref("permissions.isolateBy.privateBrowsing", true);

  let file = do_get_tempdir();
  file.append("test_default_permissions_containers");
  await IOUtils.writeUTF8(
    file.path,
    `origin\t${TEST_PERMISSION}\t1\t${TEST_ORIGIN}\n` +
      `origin\t${STRIPPED_PERMISSION}\t1\t${TEST_ORIGIN}\n`
  );

  Services.prefs.setCharPref(
    "permissions.manager.defaultsUrl",
    Services.io.newFileURI(file).spec
  );
  reloadDefaults();

  let mockCid = MockRegistrar.register(
    SITE_CONTAINER_SERVICE_CONTRACT_ID,
    gSiteContainerService
  );
  // The permission manager only asks for the containers once they are loaded,
  // and has to be running to observe that.
  testPermission(0);
  Services.obs.notifyObservers(
    null,
    "contextual-identity-service-load-finished"
  );

  registerCleanupFunction(() => {
    MockRegistrar.unregister(mockCid);
    Services.prefs.clearUserPref("permissions.isolateBy.userContext");
    Services.prefs.clearUserPref("permissions.isolateBy.privateBrowsing");
    Services.prefs.clearUserPref("permissions.manager.defaultsUrl");
    file.remove(false);
  });
});

add_task(function test_unknown_containers() {
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(0),
    "The default permission applies outside of any container"
  );
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(1),
    "Containers unknown to the permission manager don't get the default"
  );
});

add_task(function test_existing_containers() {
  addContainers([1, 2]);

  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(1),
    "The default permission was duplicated into container 1"
  );
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(2),
    "The default permission was duplicated into container 2"
  );
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(3),
    "The default permission was not duplicated into an unknown container"
  );

  Assert.equal(
    4,
    countPermissions(TEST_PERMISSION),
    "One entry outside of any container, one for private browsing and one " +
      "for each of the two containers"
  );
  Assert.equal(
    1,
    countPermissions(STRIPPED_PERMISSION),
    "Permission types which are not isolated by origin attributes are not " +
      "duplicated"
  );
});

add_task(function test_new_container() {
  addContainers([3]);

  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(3),
    "The default permission was duplicated into the new container"
  );
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(1),
    "The previously known container still has the default permission"
  );
});

function assertKnownContainersHaveDefault(message) {
  for (let userContextId of [1, 2, 3]) {
    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(userContextId),
      `Container ${userContextId} ${message}`
    );
  }
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(4),
    "Containers which were never made known still don't get the default"
  );
}

add_task(function test_reimport() {
  Services.perms.removeAll();
  assertKnownContainersHaveDefault(
    "has the default permission after removeAll"
  );

  reloadDefaults();
  assertKnownContainersHaveDefault(
    "has the default permission after reloading the defaults"
  );
});

add_task(function test_override_in_container() {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(TEST_ORIGIN),
    { userContextId: 1 }
  );
  Services.perms.addFromPrincipal(
    principal,
    TEST_PERMISSION,
    Ci.nsIPermissionManager.DENY_ACTION
  );

  Assert.equal(
    Ci.nsIPermissionManager.DENY_ACTION,
    testPermission(1),
    "The default permission of a container can be overridden"
  );
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(2),
    "Overriding the permission of a container doesn't affect other containers"
  );
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(0),
    "Overriding the permission of a container doesn't affect the default " +
      "container"
  );

  Services.perms.removeAll();
});

add_task(function test_add_default_from_principal() {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(TEST_ORIGIN_2),
    {}
  );
  Services.perms.addDefaultFromPrincipal(
    principal,
    TEST_PERMISSION,
    Ci.nsIPermissionManager.ALLOW_ACTION
  );

  let containerPrincipal =
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI(TEST_ORIGIN_2),
      { userContextId: 1 }
    );
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    Services.perms.testPermissionFromPrincipal(
      containerPrincipal,
      TEST_PERMISSION
    ),
    "A default permission added later is duplicated into known containers"
  );
});

// Removing a default outside of any container doesn't affect the containers,
// since permissions are isolated by container.
add_task(function test_removed_default_outside_container() {
  let principal = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(TEST_ORIGIN),
    {}
  );
  Services.perms.removeFromPrincipal(principal, TEST_PERMISSION);

  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(0),
    "The default permission was removed for the site"
  );

  addContainers([5]);

  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(5),
    "A container made known afterwards still gets the default"
  );
});

// The removal of a default permission is persisted as an UNKNOWN_ACTION entry.
// The database is applied before the defaults are imported, so these tests pin
// down that this startup order keeps every context's user choice.
add_task(async function test_removed_default_stays_removed_after_restart() {
  await restart();

  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(0),
    "The removed default is still removed for the site after a restart"
  );
  for (let userContextId of [1, 2, 3, 5]) {
    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(userContextId),
      `Container ${userContextId} keeps the default after a restart`
    );
  }
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermissionForAttrs({ privateBrowsingId: 1 }),
    "Private browsing keeps the default after a restart"
  );
});

add_task(async function test_removed_default_in_container_after_restart() {
  // Restores the defaults for every known container.
  Services.perms.removeAll();
  revokeForAttrs({ userContextId: 1 });

  await restart();

  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(1),
    "A default removed inside a container stays removed after a restart"
  );
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(2),
    "Other containers keep the default after a restart"
  );
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(0),
    "The site keeps the default outside of any container after a restart"
  );
});

add_task(
  async function test_removed_default_in_private_browsing_after_restart() {
    Services.perms.removeAll();
    revokeForAttrs({ privateBrowsingId: 1 });

    await restart();

    Assert.equal(
      Ci.nsIPermissionManager.UNKNOWN_ACTION,
      testPermissionForAttrs({ privateBrowsingId: 1 }),
      "A default removed in private browsing stays removed after a restart"
    );
    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(0),
      "The site keeps the default outside of private browsing after a restart"
    );
    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(1),
      "Containers keep the default after a restart"
    );
  }
);

function principal2ForAttrs(attrs) {
  return Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI(TEST_ORIGIN_2),
    attrs
  );
}

function setDefault2(permission) {
  Services.perms.addDefaultFromPrincipal(
    principal2ForAttrs({}),
    TEST_PERMISSION,
    permission
  );
}

function assertCopies2(expected, message) {
  Assert.equal(
    expected,
    Services.perms.testPermissionFromPrincipal(
      principal2ForAttrs({ privateBrowsingId: 1 }),
      TEST_PERMISSION
    ),
    `Private browsing: ${message}`
  );
  for (let userContextId of [1, 2]) {
    Assert.equal(
      expected,
      Services.perms.testPermissionFromPrincipal(
        principal2ForAttrs({ userContextId }),
        TEST_PERMISSION
      ),
      `Container ${userContextId}: ${message}`
    );
  }
}

add_task(function test_runtime_default_changes_updates_copies() {
  Services.perms.removeAll();
  setDefault2(Ci.nsIPermissionManager.ALLOW_ACTION);
  assertCopies2(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    "a default added at runtime is copied"
  );

  setDefault2(Ci.nsIPermissionManager.DENY_ACTION);
  assertCopies2(
    Ci.nsIPermissionManager.DENY_ACTION,
    "a default changed at runtime updates the copies"
  );

  setDefault2(Ci.nsIPermissionManager.UNKNOWN_ACTION);
  assertCopies2(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    "a default removed at runtime removes the copies"
  );
});

add_task(function test_user_override_in_copy_survives_default_change() {
  Services.perms.removeAll();
  setDefault2(Ci.nsIPermissionManager.ALLOW_ACTION);

  Services.perms.addFromPrincipal(
    principal2ForAttrs({ userContextId: 1 }),
    TEST_PERMISSION,
    Ci.nsIPermissionManager.PROMPT_ACTION
  );

  setDefault2(Ci.nsIPermissionManager.DENY_ACTION);
  Assert.equal(
    Ci.nsIPermissionManager.PROMPT_ACTION,
    Services.perms.testPermissionFromPrincipal(
      principal2ForAttrs({ userContextId: 1 }),
      TEST_PERMISSION
    ),
    "A user permission inside a container is kept when the default changes"
  );
  Assert.equal(
    Ci.nsIPermissionManager.DENY_ACTION,
    Services.perms.testPermissionFromPrincipal(
      principal2ForAttrs({ userContextId: 2 }),
      TEST_PERMISSION
    ),
    "Other containers get the changed default"
  );

  setDefault2(Ci.nsIPermissionManager.UNKNOWN_ACTION);
  Services.perms.removeAll();
});

add_task(function test_overridden_default_changes_updates_copies() {
  Services.perms.removeAll();
  setDefault2(Ci.nsIPermissionManager.ALLOW_ACTION);

  Services.perms.addFromPrincipal(
    principal2ForAttrs({}),
    TEST_PERMISSION,
    Ci.nsIPermissionManager.DENY_ACTION
  );

  setDefault2(Ci.nsIPermissionManager.DENY_ACTION);
  assertCopies2(
    Ci.nsIPermissionManager.DENY_ACTION,
    "changing a default overridden outside of any container updates the copies"
  );

  setDefault2(Ci.nsIPermissionManager.UNKNOWN_ACTION);
  assertCopies2(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    "removing a default overridden outside of any container removes the copies"
  );

  Services.perms.removeAll();
});

// At startup the permissions are read before ContextualIdentityService has
// loaded the containers, so the defaults are only duplicated into them once
// contextual-identity-service-load-finished arrives.
add_task(function test_default_added_after_override_outside_container() {
  Services.perms.removeAll();
  Services.perms.addFromPrincipal(
    principal2ForAttrs({}),
    TEST_PERMISSION,
    Ci.nsIPermissionManager.DENY_ACTION
  );

  setDefault2(Ci.nsIPermissionManager.ALLOW_ACTION);
  Assert.equal(
    Ci.nsIPermissionManager.DENY_ACTION,
    Services.perms.testPermissionFromPrincipal(
      principal2ForAttrs({}),
      TEST_PERMISSION
    ),
    "The override outside of any container is kept"
  );
  assertCopies2(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    "a default added after an override outside of any container is copied"
  );

  setDefault2(Ci.nsIPermissionManager.UNKNOWN_ACTION);
  Services.perms.removeAll();
});

add_task(
  async function test_removed_default_in_container_containers_loaded_late() {
    Services.perms.removeAll();
    revokeForAttrs({ userContextId: 1 });

    await Services.perms.testFlushPendingWrites();
    let userContextIds = gUserContextIds;
    gUserContextIds = [];
    reloadDefaults();

    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(0),
      "The default permission applies outside of any container after a restart"
    );

    gUserContextIds = userContextIds;
    Services.obs.notifyObservers(
      null,
      "contextual-identity-service-load-finished"
    );

    Assert.equal(
      Ci.nsIPermissionManager.UNKNOWN_ACTION,
      testPermission(1),
      "A default removed inside a container stays removed when the containers " +
        "are loaded after the permissions"
    );
    Assert.equal(
      Ci.nsIPermissionManager.ALLOW_ACTION,
      testPermission(2),
      "Other containers get the default when the containers are loaded after " +
        "the permissions"
    );

    Services.perms.removeAll();
  }
);

async function assertOverrideOutsideContainerSurvivesRestart(permission) {
  Services.perms.removeAll();
  Services.perms.addFromPrincipal(
    principalForAttrs({}),
    TEST_PERMISSION,
    permission
  );

  let contexts = [{ privateBrowsingId: 1 }, { userContextId: 1 }];
  let before = contexts.map(attrs => testPermissionForAttrs(attrs));
  Assert.deepEqual(
    before,
    [
      Ci.nsIPermissionManager.ALLOW_ACTION,
      Ci.nsIPermissionManager.ALLOW_ACTION,
    ],
    "An override outside of any container doesn't affect the copies"
  );

  await restart();

  Assert.deepEqual(
    contexts.map(attrs => testPermissionForAttrs(attrs)),
    before,
    "The copies are the same after a restart"
  );

  Services.perms.removeAll();
}

add_task(async function test_override_outside_container_after_restart() {
  await assertOverrideOutsideContainerSurvivesRestart(
    Ci.nsIPermissionManager.DENY_ACTION
  );
});

add_task(async function test_removal_outside_container_after_restart() {
  await assertOverrideOutsideContainerSurvivesRestart(
    Ci.nsIPermissionManager.UNKNOWN_ACTION
  );
});

// Removes the container and notifies about it the way
// ContextualIdentityService does when it deletes one.
function removeContainer(userContextId) {
  gUserContextIds = gUserContextIds.filter(id => id != userContextId);
  Services.obs.notifyObservers(
    null,
    "contextual-identity-deleted",
    String(userContextId)
  );
}

add_task(async function test_deleted_container() {
  Services.perms.removeAll();
  addContainers([6, 7]);
  revokeForAttrs({ userContextId: 7 });
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(6),
    "The new container has the default permission"
  );
  Assert.equal(
    Ci.nsIPermissionManager.UNKNOWN_ACTION,
    testPermission(7),
    "The default permission was removed in the other new container"
  );
  let count = countPermissions(TEST_PERMISSION);

  let deleted = [];
  let observer = (subject, topic, data) => {
    let perm = subject.QueryInterface(Ci.nsIPermission);
    if (data == "deleted" && perm.type == TEST_PERMISSION) {
      deleted.push(perm.principal.originAttributes.userContextId);
    }
  };
  Services.obs.addObserver(observer, "perm-changed");
  removeContainer(6);
  removeContainer(7);
  Services.obs.removeObserver(observer, "perm-changed");
  Assert.equal(
    count - 1,
    countPermissions(TEST_PERMISSION),
    "The copy of the default permission is gone with the deleted container"
  );
  Assert.deepEqual(
    [6],
    deleted,
    "The removal of the copy of the default permission was announced"
  );

  await restart();
  addContainers([6, 7]);
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermission(7),
    "The removal of the default wasn't kept for the deleted container"
  );

  removeContainer(6);
  removeContainer(7);
  Services.perms.removeAll();
});

async function assertRemovedOverrideRestoresDefault(
  attrs,
  context,
  addOverride
) {
  Services.perms.removeAll();
  addOverride(principalForAttrs(attrs));
  Assert.equal(
    Ci.nsIPermissionManager.DENY_ACTION,
    testPermissionForAttrs(attrs),
    `The default permission is overridden in ${context}`
  );

  revokeForAttrs(attrs);
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermissionForAttrs(attrs),
    `Removing the override in ${context} restores the default`
  );

  await restart();
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    testPermissionForAttrs(attrs),
    `${context} has the default after a restart`
  );

  Services.perms.removeAll();
}

function addDenyOverride(principal) {
  Services.perms.addFromPrincipal(
    principal,
    TEST_PERMISSION,
    Ci.nsIPermissionManager.DENY_ACTION
  );
}

add_task(async function test_removed_override_restores_default() {
  await assertRemovedOverrideRestoresDefault(
    {},
    "the default context",
    addDenyOverride
  );
});

add_task(async function test_removed_override_in_container_restores_default() {
  await assertRemovedOverrideRestoresDefault(
    { userContextId: 1 },
    "container 1",
    addDenyOverride
  );
});

add_task(
  async function test_removed_override_in_private_browsing_restores_default() {
    await assertRemovedOverrideRestoresDefault(
      { privateBrowsingId: 1 },
      "private browsing",
      principal =>
        Services.perms.addFromPrincipalAndPersistInPrivateBrowsing(
          principal,
          TEST_PERMISSION,
          Ci.nsIPermissionManager.DENY_ACTION
        )
    );
  }
);
