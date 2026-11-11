/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const BUILTIN_TYPE = "test-builtin-default";
const REMOTE_TYPE = "test-remote-default";
const OVERRIDDEN_TYPE = "test-overridden-default";
const USER_TYPE = "test-user";

const principal =
  Services.scriptSecurityManager.createContentPrincipalFromOrigin(
    "https://example.org"
  );

function writeDefaultsFile() {
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
  conv.writeString(`origin\t${BUILTIN_TYPE}\t1\t${principal.origin}\n`);
  conv.writeString(`origin\t${OVERRIDDEN_TYPE}\t1\t${principal.origin}\n`);
  ostream.close();

  return file;
}

function reloadFromDisk() {
  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");
}

function setup() {
  Services.perms.removeAll();
  Services.perms.addDefaultFromPrincipal(
    principal,
    REMOTE_TYPE,
    Services.perms.ALLOW_ACTION
  );
  Services.perms.addFromPrincipal(
    principal,
    OVERRIDDEN_TYPE,
    Services.perms.DENY_ACTION
  );
  Services.perms.addFromPrincipal(
    principal,
    USER_TYPE,
    Services.perms.ALLOW_ACTION
  );
}

function testPerm(type) {
  return Services.perms.testExactPermissionFromPrincipal(principal, type);
}

add_setup(function () {
  do_get_profile();

  let file = writeDefaultsFile();
  Services.prefs.setCharPref(
    "permissions.manager.defaultsUrl",
    "file://" + file.path
  );
  reloadFromDisk();

  registerCleanupFunction(() => {
    file.remove(false);
    Services.prefs.clearUserPref("permissions.manager.defaultsUrl");
    Services.perms.removeAll();
  });
});

add_task(async function test_keeps_defaults_removes_others() {
  setup();

  Assert.equal(testPerm(BUILTIN_TYPE), Services.perms.ALLOW_ACTION);
  Assert.equal(testPerm(REMOTE_TYPE), Services.perms.ALLOW_ACTION);
  Assert.equal(testPerm(OVERRIDDEN_TYPE), Services.perms.DENY_ACTION);
  Assert.equal(testPerm(USER_TYPE), Services.perms.ALLOW_ACTION);

  Services.perms.removePermissionsExceptDefaults(
    Services.perms.getAllForPrincipal(principal)
  );

  Assert.equal(
    testPerm(BUILTIN_TYPE),
    Services.perms.ALLOW_ACTION,
    "Built-in default is kept"
  );
  Assert.equal(
    testPerm(REMOTE_TYPE),
    Services.perms.ALLOW_ACTION,
    "Remote default is kept"
  );
  Assert.equal(
    testPerm(OVERRIDDEN_TYPE),
    Services.perms.ALLOW_ACTION,
    "Removing an override restores the default"
  );
  Assert.equal(
    testPerm(USER_TYPE),
    Services.perms.UNKNOWN_ACTION,
    "Non-default permission is removed"
  );

  reloadFromDisk();

  Assert.equal(
    testPerm(BUILTIN_TYPE),
    Services.perms.ALLOW_ACTION,
    "Built-in default is still present after reloading from disk"
  );
  Assert.equal(
    testPerm(OVERRIDDEN_TYPE),
    Services.perms.ALLOW_ACTION,
    "Restored default is still present after reloading from disk"
  );
  Assert.equal(
    testPerm(USER_TYPE),
    Services.perms.UNKNOWN_ACTION,
    "Removed permission is still gone after reloading from disk"
  );
});

add_task(async function test_removePermission_still_removes_defaults() {
  setup();

  let builtin = Services.perms.getPermissionObject(
    principal,
    BUILTIN_TYPE,
    true
  );
  Services.perms.removePermission(builtin);
  Assert.equal(testPerm(BUILTIN_TYPE), Services.perms.UNKNOWN_ACTION);

  reloadFromDisk();
  Assert.equal(
    testPerm(BUILTIN_TYPE),
    Services.perms.UNKNOWN_ACTION,
    "Explicitly removed default stays removed after reloading from disk"
  );
});

function fakePermission(aPrincipal) {
  return {
    QueryInterface: ChromeUtils.generateQI(["nsIPermission"]),
    principal: aPrincipal,
    type: USER_TYPE,
  };
}

add_task(async function test_principal_validation() {
  setup();

  let expandedPrincipal = Cu.getObjectPrincipal(
    Cu.Sandbox([principal.origin, "https://example.com"])
  );
  Assert.ok(expandedPrincipal.isExpandedPrincipal);

  Assert.throws(
    () =>
      Services.perms.removePermissionsExceptDefaults([
        fakePermission(expandedPrincipal),
      ]),
    e => e.result == Cr.NS_ERROR_INVALID_ARG,
    "Expanded principals are reported as an error"
  );

  Assert.throws(
    () =>
      Services.perms.removePermissionsExceptDefaults([fakePermission(null)]),
    e => e.result == Cr.NS_ERROR_INVALID_ARG,
    "A missing principal is reported as an error"
  );

  Services.perms.removePermissionsExceptDefaults([
    fakePermission(Services.scriptSecurityManager.getSystemPrincipal()),
    fakePermission(Services.scriptSecurityManager.createNullPrincipal({})),
  ]);
  Assert.ok(true, "System and null principals are skipped without an error");

  Assert.equal(testPerm(USER_TYPE), Services.perms.ALLOW_ACTION);
});

add_task(async function test_reports_errors_and_continues() {
  setup();

  let nullPrincipalPerm = fakePermission(null);
  let throwingPerm = {
    QueryInterface: ChromeUtils.generateQI(["nsIPermission"]),
    get principal() {
      throw Components.Exception("", Cr.NS_ERROR_NOT_AVAILABLE);
    },
    type: USER_TYPE,
  };

  Assert.throws(
    () =>
      Services.perms.removePermissionsExceptDefaults([
        nullPrincipalPerm,
        throwingPerm,
        ...Services.perms.getAllForPrincipal(principal),
      ]),
    e => e.result == Cr.NS_ERROR_INVALID_ARG,
    "The first error is reported"
  );

  Assert.equal(testPerm(BUILTIN_TYPE), Services.perms.ALLOW_ACTION);
  Assert.equal(testPerm(REMOTE_TYPE), Services.perms.ALLOW_ACTION);
  Assert.equal(
    testPerm(OVERRIDDEN_TYPE),
    Services.perms.ALLOW_ACTION,
    "Valid entries after a failing one are still processed"
  );
  Assert.equal(
    testPerm(USER_TYPE),
    Services.perms.UNKNOWN_ACTION,
    "Valid entries after a failing one are still removed"
  );
});

add_task(async function test_ignores_stale_and_invalid_entries() {
  setup();

  let userPerm = Services.perms.getPermissionObject(principal, USER_TYPE, true);
  Services.perms.removePermission(userPerm);

  Services.perms.removePermissionsExceptDefaults([
    userPerm,
    null,
    ...Services.perms.getAllForPrincipal(principal),
  ]);

  Assert.equal(testPerm(BUILTIN_TYPE), Services.perms.ALLOW_ACTION);
  Assert.equal(testPerm(REMOTE_TYPE), Services.perms.ALLOW_ACTION);
  Assert.equal(testPerm(OVERRIDDEN_TYPE), Services.perms.ALLOW_ACTION);
  Assert.equal(testPerm(USER_TYPE), Services.perms.UNKNOWN_ACTION);
});
