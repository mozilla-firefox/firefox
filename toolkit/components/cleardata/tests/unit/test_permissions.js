/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * Tests for permissions
 */

"use strict";

add_task(async function test_all_permissions() {
  const uri = Services.io.newURI("https://example.net");
  const principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );

  Services.perms.addFromPrincipal(
    principal,
    "cookie",
    Services.perms.ALLOW_ACTION
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(principal, "cookie", true),
    null
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
    Services.perms.getPermissionObject(principal, "cookie", true),
    null
  );
});

add_task(async function test_principal_permissions() {
  const uri = Services.io.newURI("https://example.net");
  const principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );

  const anotherUri = Services.io.newURI("https://example.com");
  const anotherPrincipal =
    Services.scriptSecurityManager.createContentPrincipal(anotherUri, {});

  Services.perms.addFromPrincipal(
    principal,
    "cookie",
    Services.perms.ALLOW_ACTION
  );
  Services.perms.addFromPrincipal(
    anotherPrincipal,
    "cookie",
    Services.perms.ALLOW_ACTION
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(principal, "cookie", true),
    null
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(anotherPrincipal, "cookie", true),
    null
  );

  await new Promise(aResolve => {
    Services.clearData.deleteDataFromPrincipal(
      principal,
      true /* user request */,
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  Assert.equal(
    Services.perms.getPermissionObject(principal, "cookie", true),
    null
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(anotherPrincipal, "cookie", true),
    null
  );

  await new Promise(aResolve => {
    Services.clearData.deleteData(
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      () => aResolve()
    );
  });
});

function addTestPermissions() {
  Services.perms.removeAll();

  PermissionTestUtils.add(
    "https://example.net",
    "geo",
    Services.perms.ALLOW_ACTION
  );
  PermissionTestUtils.add(
    "http://example.net",
    "cookie",
    Services.perms.DENY_ACTION
  );
  PermissionTestUtils.add(
    "https://bar.example.net",
    "geo",
    Services.perms.ALLOW_ACTION
  );
  PermissionTestUtils.add(
    "https://foo.bar.example.net",
    "geo",
    Services.perms.ALLOW_ACTION
  );
  PermissionTestUtils.add(
    "https://example.com",
    "3rdPartyStorage^https://example.net",
    Services.perms.ALLOW_ACTION
  );
  PermissionTestUtils.add(
    "https://example.com",
    "3rdPartyFrameStorage^https://example.net",
    Services.perms.ALLOW_ACTION
  );

  PermissionTestUtils.add(
    "https://example.com",
    "cookie",
    Services.perms.ALLOW_ACTION
  );
  PermissionTestUtils.add(
    "http://example.com",
    "geo",
    Services.perms.ALLOW_ACTION
  );

  Assert.equal(
    PermissionTestUtils.getPermissionObject("https://example.net", "geo", true)
      .capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "http://example.net",
      "cookie",
      true
    ).capability,
    Services.perms.DENY_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://bar.example.net",
      "geo",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://foo.bar.example.net",
      "geo",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "3rdPartyStorage^https://example.net",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "3rdPartyFrameStorage^https://example.net",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );

  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "cookie",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject("http://example.com", "geo", true)
      .capability,
    Services.perms.ALLOW_ACTION
  );
}

add_task(async function test_removeBySite() {
  addTestPermissions();

  await new Promise(aResolve => {
    Services.clearData.deleteDataFromSite(
      "example.net",
      {}, // All OriginAttributes
      true /* user request */,
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  // Should have cleared all entries associated with the site.
  Assert.ok(
    !PermissionTestUtils.getPermissionObject("https://example.net", "geo", true)
  );
  Assert.ok(
    !PermissionTestUtils.getPermissionObject(
      "http://example.net",
      "cookie",
      true
    )
  );
  Assert.ok(
    !PermissionTestUtils.getPermissionObject(
      "https://bar.example.net",
      "geo",
      true
    )
  );
  Assert.ok(
    !PermissionTestUtils.getPermissionObject(
      "https://foo.bar.example.net",
      "geo",
      true
    )
  );
  Assert.ok(
    !PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "3rdPartyStorage^https://example.net",
      true
    )
  );
  Assert.ok(
    !PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "3rdPartyFrameStorage^https://example.net",
      true
    )
  );

  // Unrelated entries should still exist.
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "cookie",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject("http://example.com", "geo", true)
      .capability,
    Services.perms.ALLOW_ACTION
  );

  Services.perms.removeAll();
});

// Test that passing an OriginAttributesPattern into deleteDataFromSite only
// clears permissions matching the pattern.
add_task(async function test_removeBySiteAndOAPattern() {
  // Private browsing permissions are only stored separately when isolated.
  const isolatePB = Services.prefs.getBoolPref(
    "permissions.isolateBy.privateBrowsing"
  );
  Services.prefs.setBoolPref("permissions.isolateBy.privateBrowsing", true);

  let principalRegular = Services.scriptSecurityManager.createContentPrincipal(
    Services.io.newURI("https://example.net"),
    {}
  );
  let principalRegularSub =
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI("https://sub.example.net"),
      {}
    );
  let principalPrivateBrowsing =
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI("https://example.net"),
      { privateBrowsingId: 1 }
    );
  let principalPrivateBrowsingSub =
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI("https://sub.example.net"),
      { privateBrowsingId: 1 }
    );
  let principalPrivateBrowsingUnrelated =
    Services.scriptSecurityManager.createContentPrincipal(
      Services.io.newURI("https://example.org"),
      { privateBrowsingId: 1 }
    );

  [
    principalRegular,
    principalRegularSub,
    principalPrivateBrowsing,
    principalPrivateBrowsingSub,
    principalPrivateBrowsingUnrelated,
  ].forEach(principal => {
    Services.perms.addFromPrincipal(
      principal,
      "geo",
      Services.perms.ALLOW_ACTION
    );
  });

  info("Clear only private browsing mode permissions for example.net");
  await new Promise(aResolve => {
    Services.clearData.deleteDataFromSite(
      "example.net",
      { privateBrowsingId: 1 },
      true,
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  info(
    "Test that only the private browsing permissions for 'example.net' have been cleared."
  );
  [principalPrivateBrowsing, principalPrivateBrowsingSub].forEach(principal => {
    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(principal, "geo"),
      Services.perms.UNKNOWN_ACTION,
      "Permission has been removed for " + principal.origin
    );
  });

  [
    principalRegular,
    principalRegularSub,
    principalPrivateBrowsingUnrelated,
  ].forEach(principal => {
    Assert.equal(
      Services.perms.testExactPermissionFromPrincipal(principal, "geo"),
      Services.perms.ALLOW_ACTION,
      "Permission still exists for " + principal.origin
    );
  });

  Services.perms.removeAll();
  Services.prefs.setBoolPref(
    "permissions.isolateBy.privateBrowsing",
    isolatePB
  );
});

add_task(async function removeByHost() {
  addTestPermissions();

  await new Promise(aResolve => {
    Services.clearData.deleteDataFromHost(
      "bar.example.net",
      true /* user request */,
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  // Should have cleared all entries associated with the host and its
  // subdomains.
  Assert.ok(
    !PermissionTestUtils.getPermissionObject(
      "https://bar.example.net",
      "geo",
      true
    )
  );
  Assert.ok(
    !PermissionTestUtils.getPermissionObject(
      "https://foo.bar.example.net",
      "geo",
      true
    )
  );

  // Unrelated entries should still exist.
  Assert.equal(
    PermissionTestUtils.getPermissionObject("https://example.net", "geo", true)
      .capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "http://example.net",
      "cookie",
      true
    ).capability,
    Services.perms.DENY_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "3rdPartyStorage^https://example.net",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "3rdPartyFrameStorage^https://example.net",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );

  Assert.equal(
    PermissionTestUtils.getPermissionObject(
      "https://example.com",
      "cookie",
      true
    ).capability,
    Services.perms.ALLOW_ACTION
  );
  Assert.equal(
    PermissionTestUtils.getPermissionObject("http://example.com", "geo", true)
      .capability,
    Services.perms.ALLOW_ACTION
  );

  Services.perms.removeAll();
});

add_task(async function test_3rdpartystorage_permissions() {
  const uri = Services.io.newURI("https://example.net");
  const principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );
  Services.perms.addFromPrincipal(
    principal,
    "cookie",
    Services.perms.ALLOW_ACTION
  );

  const anotherUri = Services.io.newURI("https://example.com");
  const anotherPrincipal =
    Services.scriptSecurityManager.createContentPrincipal(anotherUri, {});
  Services.perms.addFromPrincipal(
    anotherPrincipal,
    "cookie",
    Services.perms.ALLOW_ACTION
  );
  Services.perms.addFromPrincipal(
    anotherPrincipal,
    "3rdPartyStorage^https://example.net",
    Services.perms.ALLOW_ACTION
  );
  Services.perms.addFromPrincipal(
    anotherPrincipal,
    "3rdPartyFrameStorage^https://example.net",
    Services.perms.ALLOW_ACTION
  );

  const oneMoreUri = Services.io.newURI("https://example.org");
  const oneMorePrincipal =
    Services.scriptSecurityManager.createContentPrincipal(oneMoreUri, {});
  Services.perms.addFromPrincipal(
    oneMorePrincipal,
    "cookie",
    Services.perms.ALLOW_ACTION
  );

  Assert.notEqual(
    Services.perms.getPermissionObject(principal, "cookie", true),
    null
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(anotherPrincipal, "cookie", true),
    null
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(
      anotherPrincipal,
      "3rdPartyStorage^https://example.net",
      true
    ),
    null
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(
      anotherPrincipal,
      "3rdPartyFrameStorage^https://example.net",
      true
    ),
    null
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(oneMorePrincipal, "cookie", true),
    null
  );

  await new Promise(aResolve => {
    Services.clearData.deleteDataFromPrincipal(
      principal,
      true /* user request */,
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  Assert.equal(
    Services.perms.getPermissionObject(principal, "cookie", true),
    null
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(anotherPrincipal, "cookie", true),
    null
  );
  Assert.equal(
    Services.perms.getPermissionObject(
      anotherPrincipal,
      "3rdPartyStorage^https://example.net",
      true
    ),
    null
  );
  Assert.equal(
    Services.perms.getPermissionObject(
      anotherPrincipal,
      "3rdPartyFrameStorage^https://example.net",
      true
    ),
    null
  );
  Assert.notEqual(
    Services.perms.getPermissionObject(oneMorePrincipal, "cookie", true),
    null
  );

  await new Promise(aResolve => {
    Services.clearData.deleteData(
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      () => aResolve()
    );
  });
});

function loadDefaultPermissions(lines) {
  let file = do_get_tempdir();
  file.append("test_default_permissions");

  let ostream = Cc["@mozilla.org/network/file-output-stream;1"].createInstance(
    Ci.nsIFileOutputStream
  );
  ostream.init(file, -1, 0o666, 0);
  let conv = Cc["@mozilla.org/intl/converter-output-stream;1"].createInstance(
    Ci.nsIConverterOutputStream
  );
  conv.init(ostream, "UTF-8");
  for (let line of lines) {
    conv.writeString(line + "\n");
  }
  ostream.close();

  Services.prefs.setCharPref(
    "permissions.manager.defaultsUrl",
    "file://" + file.path
  );
  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");
  return file;
}

add_task(async function test_clear_site_permissions_preserves_defaults() {
  Services.perms.removeAll();

  // Set up a defaults file with a builtin permission.
  let file = loadDefaultPermissions(["host\tuitour\t1\texample.org"]);

  const uri = Services.io.newURI("https://example.org");
  const principal = Services.scriptSecurityManager.createContentPrincipal(
    uri,
    {}
  );

  // Verify the default permission is present.
  Assert.equal(
    Services.perms.testPermissionFromPrincipal(principal, "uitour"),
    Services.perms.ALLOW_ACTION
  );

  // Add a user-granted permission on the same origin.
  Services.perms.addFromPrincipal(
    principal,
    "geo",
    Services.perms.ALLOW_ACTION
  );

  // Clear site permissions via the clear data service.
  await new Promise(aResolve => {
    Services.clearData.deleteData(
      Ci.nsIClearDataService.CLEAR_SITE_PERMISSIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  // The default (builtin) permission should survive.
  Assert.equal(
    Services.perms.testPermissionFromPrincipal(principal, "uitour"),
    Services.perms.ALLOW_ACTION,
    "Default permission should not be removed by clearing site permissions"
  );

  // The user-granted permission should be removed.
  Assert.equal(
    Services.perms.getPermissionObject(principal, "geo", true),
    null,
    "User-granted permission should be removed by clearing site permissions"
  );

  file.remove(false);
  Services.perms.removeAll();
});

add_task(async function test_clear_by_site_preserves_defaults() {
  Services.perms.removeAll();

  let file = loadDefaultPermissions([
    "host\tuitour\t1\texample.org",
    "host\tinstall\t1\texample.org",
  ]);

  const principal =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      "https://example.org"
    );
  const testPerm = type =>
    Services.perms.testExactPermissionFromPrincipal(principal, type);

  Services.perms.addDefaultFromPrincipal(
    principal,
    "https-only-load-insecure",
    Ci.nsIHttpsOnlyModePermission.HTTPSFIRST_LOAD_INSECURE_ALLOW
  );
  Services.perms.addFromPrincipal(
    principal,
    "install",
    Services.perms.DENY_ACTION
  );
  Services.perms.addFromPrincipal(
    principal,
    "geo",
    Services.perms.ALLOW_ACTION
  );

  await new Promise(aResolve => {
    Services.clearData.deleteDataFromSite(
      "example.org",
      {},
      true,
      Ci.nsIClearDataService.CLEAR_PERMISSIONS,
      value => {
        Assert.equal(value, 0);
        aResolve();
      }
    );
  });

  Assert.equal(
    testPerm("uitour"),
    Services.perms.ALLOW_ACTION,
    "Built-in default permission is kept"
  );
  Assert.equal(
    testPerm("https-only-load-insecure"),
    Ci.nsIHttpsOnlyModePermission.HTTPSFIRST_LOAD_INSECURE_ALLOW,
    "Remote default permission is kept"
  );
  Assert.equal(
    testPerm("install"),
    Services.perms.ALLOW_ACTION,
    "Clearing a user override restores the default permission"
  );
  Assert.equal(
    testPerm("geo"),
    Services.perms.UNKNOWN_ACTION,
    "User-granted permission is removed"
  );

  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  Assert.equal(
    testPerm("uitour"),
    Services.perms.ALLOW_ACTION,
    "Built-in default permission is still present after a reload"
  );
  Assert.equal(
    testPerm("install"),
    Services.perms.ALLOW_ACTION,
    "Restored default permission is still present after a reload"
  );

  file.remove(false);
  Services.prefs.clearUserPref("permissions.manager.defaultsUrl");
  Services.perms.removeAll();
});
