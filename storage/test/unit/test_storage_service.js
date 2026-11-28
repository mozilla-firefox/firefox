/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file tests the functions of mozIStorageService except for
// openSpecialDatabase, which is tested by test_storage_service_special.js and
// openUnsharedDatabase, which is tested by test_storage_service_unshared.js.

function copyToTempDir(aFileName, aNewName) {
  let tempDir = do_get_tempdir();
  do_get_file(aFileName).copyTo(tempDir, aNewName);
  let file = tempDir.clone();
  file.append(aNewName);
  return file;
}

function test_openDatabase_null_file() {
  try {
    Services.storage.openDatabase(null);
    do_throw("We should not get here!");
  } catch (e) {
    print(e);
    print("e.result is " + e.result);
    Assert.equal(Cr.NS_ERROR_INVALID_ARG, e.result);
  }
}

function test_openDatabase_file_DNE() {
  // the file should be created after calling
  var db = getTestDB();
  Assert.ok(!db.exists());
  Services.storage.openDatabase(db);
  Assert.ok(db.exists());
}

function test_openDatabase_file_exists() {
  // it should already exist from our last test
  var db = getTestDB();
  Assert.ok(db.exists());
  Services.storage.openDatabase(db);
  Assert.ok(db.exists());
}

function test_corrupt_db_throws_with_openDatabase() {
  let db = copyToTempDir("corruptDB.sqlite", "corruptDB.sqlite");

  try {
    getDatabase(db);
    do_throw("should not be here");
  } catch (e) {
    Assert.equal(Cr.NS_ERROR_FILE_CORRUPTED, e.result);
  }

  Assert.equal(
    Glean.sqliteStore.query.get(db.leafName, "corrupt").testGetValue(),
    1
  );
}

function test_fake_db_throws_with_openDatabase() {
  let db = copyToTempDir("fakeDB.sqlite", "fakeDB.sqlite");

  try {
    getDatabase(db);
    do_throw("should not be here");
  } catch (e) {
    Assert.equal(Cr.NS_ERROR_FILE_CORRUPTED, e.result);
  }

  Assert.equal(
    Glean.sqliteStore.query.get(db.leafName, "corrupt").testGetValue(),
    1
  );
}

function test_openDatabase_directory() {
  let dir = getTestDB().parent;
  dir.append("test_storage_temp");
  if (dir.exists()) {
    dir.remove(true);
  }
  dir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o755);
  Assert.ok(dir.exists());

  let encrypted = Services.prefs.getBoolPref(
    "security.storage.encryption.sqlite.enabled",
    false
  );

  try {
    getDatabase(dir);
    do_throw("should not be here");
  } catch (e) {
    // The plain VFS rejects a directory-as-database open with
    // NS_ERROR_FILE_ACCESS_DENIED; with encryption, obfsvfs rejects it earlier
    // (before the lower VFS reports the access error) with NS_ERROR_FAILURE.
    Assert.equal(
      encrypted ? Cr.NS_ERROR_FAILURE : Cr.NS_ERROR_FILE_ACCESS_DENIED,
      e.result
    );
  }

  // The plain VFS records an "access" open error here; with encryption the open
  // is rejected by obfsvfs before mozStorage reaches its open-error telemetry,
  // so nothing is recorded.
  if (!encrypted) {
    Assert.equal(
      Glean.sqliteStore.open.get("test_storage_temp", "access").testGetValue(),
      1
    );
  }

  dir.remove(true);
}

function test_read_gooddb() {
  let file = copyToTempDir("goodDB.sqlite", "readGoodDB.sqlite");
  let db = getDatabase(file);

  db.executeSimpleSQL("SELECT * FROM Foo;");

  Assert.equal(
    Glean.sqliteStore.query.get(file.leafName, "failure").testGetValue(),
    null
  );

  let stmt = db.createStatement("SELECT id from Foo");

  while (true) {
    if (!stmt.executeStep()) {
      break;
    }
  }

  stmt.finalize();

  Assert.equal(
    Glean.sqliteStore.query.get(file.leafName, "failure").testGetValue(),
    null
  );

  Assert.throws(
    () => db.executeSimpleSQL("INSERT INTO Foo (rowid) VALUES ('test');"),
    /NS_ERROR_UNEXPECTED/,
    "Executing sql should fail."
  );
  Assert.equal(
    Glean.sqliteStore.query.get(file.leafName, "misuse").testGetValue(),
    1
  );

  db.close();
}

function test_read_baddb() {
  let file = copyToTempDir("baddataDB.sqlite", "readBaddataDB.sqlite");
  let db = getDatabase(file);

  Assert.throws(
    () => db.executeSimpleSQL("SELECT * FROM Foo"),
    /NS_ERROR_FILE_CORRUPTED/,
    "Executing sql should fail."
  );

  Assert.equal(
    Glean.sqliteStore.query.get(file.leafName, "corrupt").testGetValue(),
    1
  );

  let stmt = db.createStatement("SELECT * FROM Foo");
  Assert.throws(
    () => stmt.executeStep(),
    /NS_ERROR_FILE_CORRUPTED/,
    "Executing a statement should fail."
  );

  Assert.equal(
    Glean.sqliteStore.query.get(file.leafName, "corrupt").testGetValue(),
    2
  );

  stmt.finalize();
  db.close();
}

function test_busy_telemetry() {
  let file = copyToTempDir("goodDB.sqlite", "goodDB.sqlite");
  let conn1 = Services.storage.openUnsharedDatabase(file);
  let conn2 = Services.storage.openUnsharedDatabase(file);

  conn1.beginTransaction();
  conn1.executeSimpleSQL("CREATE TABLE test (id INTEGER PRIMARY KEY)");

  try {
    Assert.throws(
      () =>
        conn2.executeSimpleSQL(
          "CREATE TABLE test_busy (id INTEGER PRIMARY KEY)"
        ),
      /NS_ERROR_STORAGE_BUSY/,
      "Nested transaction on second connection should fail"
    );
    Assert.equal(
      Glean.sqliteStore.query.get(file.leafName, "busy").testGetValue(),
      1
    );
  } finally {
    conn1.rollbackTransaction();
    conn1.close();
    conn2.close();
  }
}

function test_busy_open_telemetry() {
  let file = copyToTempDir("goodDB.sqlite", "busyOpen.sqlite");
  let conn1 = Services.storage.openUnsharedDatabase(file);
  conn1.executeSimpleSQL("BEGIN EXCLUSIVE");

  try {
    Assert.throws(
      () => Services.storage.openUnsharedDatabase(file),
      /NS_ERROR_STORAGE_BUSY/,
      "Opening a database locked by another connection should fail"
    );
    Assert.equal(
      Glean.sqliteStore.open.get(file.leafName, "busy").testGetValue(),
      1
    );
  } finally {
    conn1.executeSimpleSQL("ROLLBACK");
    conn1.close();
  }
}

var tests = [
  test_openDatabase_null_file,
  test_openDatabase_file_DNE,
  test_openDatabase_file_exists,
  test_corrupt_db_throws_with_openDatabase,
  test_fake_db_throws_with_openDatabase,
  test_openDatabase_directory,
  test_read_gooddb,
  test_read_baddb,
  test_busy_telemetry,
  test_busy_open_telemetry,
];

function run_test() {
  for (var i = 0; i < tests.length; i++) {
    tests[i]();
    Services.fog.testResetFOG();
  }

  cleanup();
}
