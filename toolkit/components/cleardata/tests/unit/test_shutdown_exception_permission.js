/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for the ShutdownExceptionsCleaner.
 *
 * Bug 1767271 split the clear-on-shutdown exception out of the generic
 * "cookie" permission into "persist-data-on-shutdown".
 */

"use strict";

// CLEAR_SHUTDOWN_EXCEPTIONS clears the dedicated persist-data-on-shutdown
// permission and nothing else.
add_task(
  async function test_removing_shutdown_exception_permission_only_clears_persist_data_on_shutdown() {
    let uri = Services.io.newURI("https://example.net");
    let principal = Services.scriptSecurityManager.createContentPrincipal(
      uri,
      {}
    );

    Services.perms.addFromPrincipal(
      principal,
      "persist-data-on-shutdown",
      Services.perms.ALLOW_ACTION
    );

    Services.perms.addFromPrincipal(
      principal,
      "notcookie",
      Services.perms.ALLOW_ACTION
    );

    await new Promise(aResolve => {
      Services.clearData.deleteData(
        Ci.nsIClearDataService.CLEAR_SHUTDOWN_EXCEPTIONS,
        value => {
          Assert.equal(value, 0);
          aResolve();
        }
      );
    });

    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(
        principal,
        "persist-data-on-shutdown"
      ),
      Services.perms.UNKNOWN_ACTION,
      "the persist-data-on-shutdown permission has been removed"
    );
    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(principal, "notcookie"),
      Services.perms.ALLOW_ACTION,
      "the other permission has not been removed"
    );

    Services.perms.removeAll();
  }
);

// CLEAR_SITE_PERMISSIONS clears cookie permissions like any other site
// permission (post Bug 1767271 they are no longer special) but skips
// persist-data-on-shutdown which is owned by ShutdownExceptionsCleaner.
add_task(
  async function test_removing_site_permissions_skips_persist_data_on_shutdown() {
    let uri = Services.io.newURI("https://example.net");
    let principal = Services.scriptSecurityManager.createContentPrincipal(
      uri,
      {}
    );

    Services.perms.addFromPrincipal(
      principal,
      "persist-data-on-shutdown",
      Services.perms.ALLOW_ACTION
    );
    Services.perms.addFromPrincipal(
      principal,
      "cookie",
      Services.perms.ALLOW_ACTION
    );
    Services.perms.addFromPrincipal(
      principal,
      "notcookie",
      Services.perms.ALLOW_ACTION
    );

    await new Promise(aResolve => {
      Services.clearData.deleteData(
        Ci.nsIClearDataService.CLEAR_SITE_PERMISSIONS,
        value => {
          Assert.equal(value, 0);
          aResolve();
        }
      );
    });

    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(
        principal,
        "persist-data-on-shutdown"
      ),
      Services.perms.ALLOW_ACTION,
      "persist-data-on-shutdown survives CLEAR_SITE_PERMISSIONS"
    );
    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(principal, "cookie"),
      Services.perms.UNKNOWN_ACTION,
      "cookie permission is cleared by CLEAR_SITE_PERMISSIONS"
    );
    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(principal, "notcookie"),
      Services.perms.UNKNOWN_ACTION,
      "the other permission has been removed"
    );

    Services.perms.removeAll();
  }
);

// CLEAR_PERMISSIONS clears everything, including persist-data-on-shutdown.
add_task(async function test_removing_all_permissions() {
  let uri = Services.io.newURI("https://example.net");
  const principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );

  Services.perms.addFromPrincipal(
    principal,
    "persist-data-on-shutdown",
    Services.perms.ALLOW_ACTION
  );
  Services.perms.addFromPrincipal(
    principal,
    "notcookie",
    Services.perms.ALLOW_ACTION
  );

  await new Promise(aResolve => {
    Services.clearData.deleteData(
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  Assert.equal(
    Services.perms.testExactPermissionFromPrincipal(
      principal,
      "persist-data-on-shutdown"
    ),
    Services.perms.UNKNOWN_ACTION,
    "the persist-data-on-shutdown permission has been removed"
  );
  Assert.equal(
    Services.perms.testExactPermissionFromPrincipal(principal, "notcookie"),
    Services.perms.UNKNOWN_ACTION,
    "the other permission has been removed"
  );

  Services.perms.removeAll();
});

add_task(async function test_removeBySiteAndOAPattern() {
  let principalRegular = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI("https://example.net"),
    {}
  );
  let principalRegularSub =
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI("https://sub.example.net"),
      {}
    );
  let principalUnrelated =
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI("https://example.org"),
      {}
    );

  info(
    "add persist-data-on-shutdown (== clear on shutdown exception) permissions"
  );
  [principalRegular, principalRegularSub, principalUnrelated].forEach(
    principal => {
      Services.perms.addFromPrincipal(
        principal,
        "persist-data-on-shutdown",
        Services.perms.ALLOW_ACTION
      );
    }
  );

  info("Clear shutdown exceptions for schemeless site example.net");
  await new Promise(aResolve => {
    Services.clearData.deleteDataFromSite(
      "example.net",
      {},
      true,
      Ci.nsIClearDataService.CLEAR_SHUTDOWN_EXCEPTIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  [principalRegular, principalRegularSub].forEach(principal => {
    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(
        principal,
        "persist-data-on-shutdown"
      ),
      Services.perms.UNKNOWN_ACTION,
      "persist-data-on-shutdown permission has been removed for " +
        principal.origin
    );
  });

  [principalUnrelated].forEach(principal => {
    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(
        principal,
        "persist-data-on-shutdown"
      ),
      Services.perms.ALLOW_ACTION,
      "persist-data-on-shutdown permission still exists for " + principal.origin
    );
  });

  Services.perms.removeAll();
});

// persist-data-on-shutdown permissions are never isolated by OA in the
// permission manager, see `kStripOAPermissions` in `PermissionManager.cpp`.
// Clearing a site with a container or private browsing pattern must still
// remove them, even when permissions are otherwise isolated.
add_task(
  {
    pref_set: [
      ["permissions.isolateBy.userContext", true],
      ["permissions.isolateBy.privateBrowsing", true],
    ],
  },
  async function test_removeBySiteAndOAPatternWithIsolation() {
    for (let pattern of [{ userContextId: 2 }, { privateBrowsingId: 1 }]) {
      let principal = Services.scriptSecurityManager.createContentPrincipal(
        Services.io.newURI("https://example.net"),
        pattern
      );
      let principalUnrelated =
        Services.scriptSecurityManager.createContentPrincipal(
          Services.io.newURI("https://example.org"),
          pattern
        );

      for (let p of [principal, principalUnrelated]) {
        Services.perms.addFromPrincipal(
          p,
          "persist-data-on-shutdown",
          Services.perms.ALLOW_ACTION
        );
      }

      await new Promise(aResolve => {
        Services.clearData.deleteDataFromSite(
          "example.net",
          pattern,
          true,
          Ci.nsIClearDataService.CLEAR_SHUTDOWN_EXCEPTIONS,
          value => {
            Assert.equal(value, 0);
            aResolve();
          }
        );
      });

      Assert.equal(
        Services.perms.testExactPermissionFromPrincipal(
          principal,
          "persist-data-on-shutdown"
        ),
        Services.perms.UNKNOWN_ACTION,
        `persist-data-on-shutdown permission has been removed for pattern ${JSON.stringify(pattern)}`
      );
      Assert.equal(
        Services.perms.testExactPermissionFromPrincipal(
          principalUnrelated,
          "persist-data-on-shutdown"
        ),
        Services.perms.ALLOW_ACTION,
        `persist-data-on-shutdown permission still exists for example.org with pattern ${JSON.stringify(pattern)}`
      );

      Services.perms.removeAll();
    }
  }
);
