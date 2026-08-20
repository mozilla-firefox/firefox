/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

var PERMISSIONS_FILE_NAME = "permissions.sqlite";

/*
 * Prevent the nsINavHistoryService from being avaliable for the migration
 */

var CONTRACT_ID = "@mozilla.org/browser/nav-history-service;1";
var factory = {
  createInstance() {
    throw new Error("There is no history service");
  },
  QueryInterface: ChromeUtils.generateQI(["nsIFactory"]),
};

var newClassID = Services.uuid.generateUUID();

var registrar = Components.manager.QueryInterface(Ci.nsIComponentRegistrar);
var oldClassID = registrar.contractIDToCID(CONTRACT_ID);
registrar.registerFactory(newClassID, "", CONTRACT_ID, factory);

function cleanupFactory() {
  registrar.unregisterFactory(newClassID, factory);
  registrar.registerFactory(oldClassID, "", CONTRACT_ID, null);
}

function GetPermissionsFile(profile) {
  let file = profile.clone();
  file.append(PERMISSIONS_FILE_NAME);
  return file;
}

/*
 * Done nsINavHistoryService code
 */

add_task(function test() {
  // Create and set up the permissions database.
  Services.prefs.setCharPref("permissions.manager.defaultsUrl", "");
  let profile = do_get_profile();

  // Make sure that we can't resolve the nsINavHistoryService
  try {
    Cc["@mozilla.org/browser/nav-history-service;1"].getService(
      Ci.nsINavHistoryService
    );
    Assert.ok(false, "There shouldn't have been a nsINavHistoryService");
  } catch (e) {
    Assert.ok(true, "There wasn't a nsINavHistoryService");
  }

  // We need to execute a pm method to be sure that the DB is fully
  // initialized.
  var pm = Services.perms;
  Assert.greaterOrEqual(
    pm.all.length,
    0,
    "Permission manager not initialized?"
  );

  let db = Services.storage.openDatabase(GetPermissionsFile(profile));
  db.schemaVersion = 4;
  db.executeSimpleSQL("DROP TABLE moz_perms");
  db.executeSimpleSQL("DROP TABLE moz_hosts");

  db.executeSimpleSQL(
    "CREATE TABLE moz_hosts (" +
      " id INTEGER PRIMARY KEY" +
      ",host TEXT" +
      ",type TEXT" +
      ",permission INTEGER" +
      ",expireType INTEGER" +
      ",expireTime INTEGER" +
      ",modificationTime INTEGER" +
      ",appId INTEGER" +
      ",isInBrowserElement INTEGER" +
      ")"
  );

  let stmtInsert = db.createStatement(
    "INSERT INTO moz_hosts (" +
      "id, host, type, permission, expireType, expireTime, modificationTime, appId, isInBrowserElement" +
      ") VALUES (" +
      ":id, :host, :type, :permission, :expireType, :expireTime, :modificationTime, :appId, :isInBrowserElement" +
      ")"
  );

  let id = 0;

  function insertHost(
    host,
    type,
    permission,
    expireType,
    expireTime,
    modificationTime,
    appId,
    isInBrowserElement
  ) {
    let thisId = id++;

    stmtInsert.bindByName("id", thisId);
    stmtInsert.bindByName("host", host);
    stmtInsert.bindByName("type", type);
    stmtInsert.bindByName("permission", permission);
    stmtInsert.bindByName("expireType", expireType);
    stmtInsert.bindByName("expireTime", expireTime);
    stmtInsert.bindByName("modificationTime", modificationTime);
    stmtInsert.bindByName("appId", appId);
    stmtInsert.bindByName("isInBrowserElement", isInBrowserElement);

    try {
      stmtInsert.execute();
    } finally {
      stmtInsert.reset();
    }

    return {
      id: thisId,
      host,
      type,
      permission,
      expireType,
      expireTime,
      modificationTime,
      appId,
      isInBrowserElement,
    };
  }

  // Add some rows to the database
  // eslint-disable-next-line no-unused-vars
  let created = [
    insertHost("foo.com", "A", 1, 0, 0, 0, 0, false),
    insertHost("foo.com", "C", 1, 0, 0, 0, 0, false),
    insertHost("foo.com", "A", 1, 0, 0, 0, 1000, false),
    insertHost("foo.com", "A", 1, 0, 0, 0, 2000, true),
    insertHost("sub.foo.com", "B", 1, 0, 0, 0, 0, false),
    insertHost("subber.sub.foo.com", "B", 1, 0, 0, 0, 0, false),
    insertHost("bar.ca", "B", 1, 0, 0, 0, 0, false),
    insertHost("bar.ca", "B", 1, 0, 0, 0, 1000, false),
    insertHost("bar.ca", "A", 1, 0, 0, 0, 1000, true),
    insertHost("localhost", "A", 1, 0, 0, 0, 0, false),
    insertHost("127.0.0.1", "A", 1, 0, 0, 0, 0, false),
    insertHost("file:///some/path/to/file.html", "A", 1, 0, 0, 0, 0, false),
    insertHost("file:///another/file.html", "A", 1, 0, 0, 0, 0, false),
    insertHost(
      "moz-nullprincipal:{8695105a-adbe-4e4e-8083-851faa5ca2d7}",
      "A",
      1,
      0,
      0,
      0,
      0,
      false
    ),
    insertHost(
      "moz-nullprincipal:{12ahjksd-akjs-asd3-8393-asdu2189asdu}",
      "B",
      1,
      0,
      0,
      0,
      0,
      false
    ),
    insertHost("<file>", "A", 1, 0, 0, 0, 0, false),
    insertHost("<file>", "B", 1, 0, 0, 0, 0, false),
  ];

  // CLose the db connection
  stmtInsert.finalize();
  db.close();
  stmtInsert = null;
  db = null;

  let expected = [
    ["http://foo.com", "A", 1, 0, 0],
    ["http://foo.com", "C", 1, 0, 0],
    ["http://sub.foo.com", "B", 1, 0, 0],
    ["http://subber.sub.foo.com", "B", 1, 0, 0],

    ["https://foo.com", "A", 1, 0, 0],
    ["https://foo.com", "C", 1, 0, 0],
    ["https://sub.foo.com", "B", 1, 0, 0],
    ["https://subber.sub.foo.com", "B", 1, 0, 0],

    // bar.ca will have both http:// and https:// for all entries, because there are no associated history entries
    ["http://bar.ca", "B", 1, 0, 0],
    ["https://bar.ca", "B", 1, 0, 0],
    ["http://bar.ca", "A", 1, 0, 0],
    ["https://bar.ca", "A", 1, 0, 0],
    ["file:///some/path/to/file.html", "A", 1, 0, 0],
    ["file:///another/file.html", "A", 1, 0, 0],

    // Make sure that we also support localhost, and IP addresses
    ["http://localhost", "A", 1, 0, 0],
    ["https://localhost", "A", 1, 0, 0],
    ["http://127.0.0.1", "A", 1, 0, 0],
    ["https://127.0.0.1", "A", 1, 0, 0],
  ];

  let found = expected.map(() => 0);

  // This will force the permission-manager to reload the data.
  Services.obs.notifyObservers(null, "testonly-reload-permissions-from-disk");

  // Force initialization of the PermissionManager
  for (let permission of Services.perms.all) {
    let isExpected = false;

    expected.forEach((it, i) => {
      if (
        permission.principal.origin == it[0] &&
        permission.type == it[1] &&
        permission.capability == it[2] &&
        permission.expireType == it[3] &&
        permission.expireTime == it[4]
      ) {
        isExpected = true;
        found[i]++;
      }
    });

    Assert.ok(
      isExpected,
      "Permission " +
        (isExpected ? "should" : "shouldn't") +
        " be in permission database: " +
        permission.principal.origin +
        ", " +
        permission.type +
        ", " +
        permission.capability +
        ", " +
        permission.expireType +
        ", " +
        permission.expireTime
    );
  }

  found.forEach((count, i) => {
    Assert.equal(
      count,
      1,
      "Expected count = 1, got count = " +
        count +
        " for permission " +
        expected[i]
    );
  });

  // Check to make sure that all of the tables which we care about are present
  {
    db = Services.storage.openDatabase(GetPermissionsFile(profile));
    Assert.ok(db.tableExists("moz_perms"));
    Assert.ok(db.tableExists("moz_hosts"));
    Assert.ok(!db.tableExists("moz_hosts_is_backup"));
    Assert.ok(!db.tableExists("moz_perms_v6"));

    // The moz_hosts table should still exist but be empty
    let mozHostsCount = db.createStatement("SELECT count(*) FROM moz_hosts");
    try {
      mozHostsCount.executeStep();
      Assert.equal(mozHostsCount.getInt64(0), 0);
    } finally {
      mozHostsCount.finalize();
    }

    db.close();
  }

  cleanupFactory();
});
