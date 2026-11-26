/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const TEST_URI = Services.io.newURI("http://example.com");
const TEST_PERMISSION = "test/oastrip";
const TEST_PERMISSION2 = "test/oastrip2";
const TEST_PERMISSION3 = "test/oastrip3";

// List of permissions which are not isolated by private browsing or user context
// as per array kStripOAPermissions in PermissionManager.cpp
const STRIPPED_PERMS = [
  "cookie",
  "https-only-load-insecure",
  "ipp-vpn",
  "install",
  "persist-data-on-shutdown",
];

let principal = Services.scriptSecurityManager.createContentPrincipal(
  TEST_URI,
  {}
);
let principalPrivateBrowsing =
  Services.scriptSecurityManager.createContentPrincipal(TEST_URI, {
    privateBrowsingId: 1,
  });
let principalUserContext1 =
  Services.scriptSecurityManager.createContentPrincipal(TEST_URI, {
    userContextId: 1,
  });
let principalUserContext2 =
  Services.scriptSecurityManager.createContentPrincipal(TEST_URI, {
    userContextId: 2,
  });

function testOAIsolation(permIsolateUserContext, permIsolatePrivateBrowsing) {
  info(
    `testOAIsolation: permIsolateUserContext: ${permIsolateUserContext}; permIsolatePrivateBrowsing: ${permIsolatePrivateBrowsing}`
  );

  let pm = Services.perms;

  Services.prefs.setBoolPref(
    "permissions.isolateBy.userContext",
    permIsolateUserContext
  );
  Services.prefs.setBoolPref(
    "permissions.isolateBy.privateBrowsing",
    permIsolatePrivateBrowsing
  );

  // Set test permission for normal browsing
  pm.addFromPrincipal(principal, TEST_PERMISSION, pm.ALLOW_ACTION);

  // Check normal browsing permission
  Assert.equal(
    Ci.nsIPermissionManager.ALLOW_ACTION,
    pm.testPermissionFromPrincipal(principal, TEST_PERMISSION)
  );
  // normal browsing => user context 1
  Assert.equal(
    permIsolateUserContext
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.ALLOW_ACTION,
    pm.testPermissionFromPrincipal(principalUserContext1, TEST_PERMISSION)
  );
  // normal browsing => user context 2
  Assert.equal(
    permIsolateUserContext
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.ALLOW_ACTION,
    pm.testPermissionFromPrincipal(principalUserContext2, TEST_PERMISSION)
  );
  // normal browsing => private browsing
  Assert.equal(
    permIsolatePrivateBrowsing
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.ALLOW_ACTION,
    pm.testPermissionFromPrincipal(principalPrivateBrowsing, TEST_PERMISSION)
  );

  // Set permission for private browsing
  pm.addFromPrincipal(
    principalPrivateBrowsing,
    TEST_PERMISSION2,
    pm.DENY_ACTION
  );

  // Check private browsing permission
  Assert.equal(
    Ci.nsIPermissionManager.DENY_ACTION,
    pm.testPermissionFromPrincipal(principalPrivateBrowsing, TEST_PERMISSION2)
  );
  // private browsing => normal browsing
  Assert.equal(
    permIsolatePrivateBrowsing
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.DENY_ACTION,
    pm.testPermissionFromPrincipal(principal, TEST_PERMISSION2)
  );
  // private browsing => user context 1
  Assert.equal(
    permIsolatePrivateBrowsing || permIsolateUserContext
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.DENY_ACTION,
    pm.testPermissionFromPrincipal(principalUserContext1, TEST_PERMISSION2)
  );
  // private browsing => user context 2
  Assert.equal(
    permIsolatePrivateBrowsing || permIsolateUserContext
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.DENY_ACTION,
    pm.testPermissionFromPrincipal(principalUserContext2, TEST_PERMISSION2)
  );

  // Set permission for user context 1
  pm.addFromPrincipal(
    principalUserContext1,
    TEST_PERMISSION3,
    pm.PROMPT_ACTION
  );

  // Check user context 1 permission
  Assert.equal(
    Ci.nsIPermissionManager.PROMPT_ACTION,
    pm.testPermissionFromPrincipal(principalUserContext1, TEST_PERMISSION3)
  );

  // user context 1 => normal browsing
  Assert.equal(
    permIsolateUserContext
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.PROMPT_ACTION,
    pm.testPermissionFromPrincipal(principal, TEST_PERMISSION3)
  );
  // user context 1 => user context 2
  Assert.equal(
    permIsolateUserContext
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.PROMPT_ACTION,
    pm.testPermissionFromPrincipal(principalUserContext2, TEST_PERMISSION3)
  );
  // user context 1 => private browsing
  Assert.equal(
    permIsolatePrivateBrowsing || permIsolateUserContext
      ? Ci.nsIPermissionManager.UNKNOWN_ACTION
      : Ci.nsIPermissionManager.PROMPT_ACTION,
    pm.testPermissionFromPrincipal(principalPrivateBrowsing, TEST_PERMISSION3)
  );

  pm.removeAll();

  // Modifying an non-isolated/stripped permission should affect all browsing contexts,
  // independently of permission isolation pref state
  STRIPPED_PERMS.forEach(perm => {
    info("Testing stripped permission " + perm);

    // Add a permission for the normal window
    pm.addFromPrincipal(principal, perm, pm.ALLOW_ACTION);
    Assert.equal(
      pm.testPermissionFromPrincipal(principalPrivateBrowsing, perm),
      Ci.nsIPermissionManager.ALLOW_ACTION
    );
    Assert.equal(
      pm.testPermissionFromPrincipal(principalUserContext1, perm),
      Ci.nsIPermissionManager.ALLOW_ACTION
    );
    Assert.equal(
      pm.testPermissionFromPrincipal(principalUserContext2, perm),
      Ci.nsIPermissionManager.ALLOW_ACTION
    );
    let permission = pm.getPermissionObject(principal, perm, true);
    for (let p of [
      principal,
      principalPrivateBrowsing,
      principalUserContext1,
      principalUserContext2,
    ]) {
      Assert.ok(permission.matches(p, true));
    }

    // Remove the permission from private window
    pm.removeFromPrincipal(principalPrivateBrowsing, perm);
    Assert.equal(
      pm.testPermissionFromPrincipal(principal, perm),
      Ci.nsIPermissionManager.UNKNOWN_ACTION
    );
    Assert.equal(
      pm.testPermissionFromPrincipal(principalUserContext1, perm),
      Ci.nsIPermissionManager.UNKNOWN_ACTION
    );
    Assert.equal(
      pm.testPermissionFromPrincipal(principalUserContext2, perm),
      Ci.nsIPermissionManager.UNKNOWN_ACTION
    );

    // Set a permission for a normal window and then override it by adding it to container 2 again
    pm.addFromPrincipal(principal, perm, pm.PROMPT_ACTION);
    pm.addFromPrincipal(principal, TEST_PERMISSION, pm.ALLOW_ACTION);
    pm.addFromPrincipal(principalUserContext2, perm, pm.DENY_ACTION);

    let principalPerms = pm.getAllForPrincipal(principalPrivateBrowsing, perm);

    Assert.ok(
      principalPerms.some(p => p.type == perm && p.capability == pm.DENY_ACTION)
    );
    if (permIsolatePrivateBrowsing) {
      Assert.equal(principalPerms.length, 1);
      Assert.ok(
        principalPerms.some(
          p => p.type == perm && p.capability == pm.DENY_ACTION
        )
      );
    } else {
      Assert.equal(principalPerms.length, 2);
      Assert.ok(
        principalPerms.some(
          p => p.type == TEST_PERMISSION && p.capability == pm.ALLOW_ACTION
        )
      );
    }

    // Cleanup
    pm.removeAll();
  });
}

add_task(async function do_test() {
  // Test all pref combinations and check if principals with different origin attributes
  // are isolated.
  testOAIsolation(true, true);
  testOAIsolation(true, false);
  testOAIsolation(false, true);
  testOAIsolation(false, false);
});
