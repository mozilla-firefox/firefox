/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file tests the functions of mozIStorageConnection

function fetchAllNames(conn) {
  let names = [];
  let stmt = conn.createStatement(`SELECT name FROM test ORDER BY name`);
  while (stmt.executeStep()) {
    names.push(stmt.getUTF8String(0));
  }
  stmt.finalize();
  return names;
}

// Test Functions

add_task(async function test_connectionReady_open() {
  // there doesn't seem to be a way for the connection to not be ready (unless
  // we close it with mozIStorageConnection::Close(), but we don't for this).
  // It can only fail if GetPath fails on the database file, or if we run out
  // of memory trying to use an in-memory database

  var msc = getOpenedDatabase();
  Assert.ok(msc.connectionReady);
});

add_task(async function test_connectionReady_closed() {
  // This also tests mozIStorageConnection::Close()

  var msc = getOpenedDatabase();
  msc.close();
  Assert.ok(!msc.connectionReady);
  gDBConn = null; // this is so later tests don't start to fail.
});

add_task(async function test_databaseFile() {
  var msc = getOpenedDatabase();
  Assert.ok(getTestDB().equals(msc.databaseFile));
});

add_task(async function test_tableExists_not_created() {
  var msc = getOpenedDatabase();
  Assert.ok(!msc.tableExists("foo"));
});

add_task(async function test_indexExists_not_created() {
  var msc = getOpenedDatabase();
  Assert.ok(!msc.indexExists("foo"));
});

add_task(async function test_temp_tableExists_and_indexExists() {
  var msc = getOpenedDatabase();
  msc.executeSimpleSQL(
    "CREATE TEMP TABLE test_temp(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT)"
  );
  Assert.ok(msc.tableExists("test_temp"));

  msc.executeSimpleSQL("CREATE INDEX test_temp_ind ON test_temp (name)");
  Assert.ok(msc.indexExists("test_temp_ind"));

  msc.executeSimpleSQL("DROP INDEX test_temp_ind");
  msc.executeSimpleSQL("DROP TABLE test_temp");
});

add_task(async function test_createTable_not_created() {
  var msc = getOpenedDatabase();
  msc.createTable("test", "id INTEGER PRIMARY KEY, name TEXT");
  Assert.ok(msc.tableExists("test"));
});

add_task(async function test_indexExists_created() {
  var msc = getOpenedDatabase();
  msc.executeSimpleSQL("CREATE INDEX name_ind ON test (name)");
  Assert.ok(msc.indexExists("name_ind"));
});

add_task(async function test_createTable_already_created() {
  var msc = getOpenedDatabase();
  Assert.ok(msc.tableExists("test"));
  Assert.throws(
    () => msc.createTable("test", "id INTEGER PRIMARY KEY, name TEXT"),
    /NS_ERROR_FAILURE/
  );
});

add_task(async function test_attach_createTable_tableExists_indexExists() {
  var msc = getOpenedDatabase();
  var file = do_get_profile();
  file.append("storage_attach.sqlite");
  var msc2 = getDatabase(file);
  msc.executeSimpleSQL("ATTACH DATABASE '" + file.path + "' AS sample");

  Assert.ok(!msc.tableExists("sample.test"));
  msc.createTable("sample.test", "id INTEGER PRIMARY KEY, name TEXT");
  Assert.ok(msc.tableExists("sample.test"));
  Assert.throws(
    () => msc.createTable("sample.test", "id INTEGER PRIMARY KEY, name TEXT"),
    /NS_ERROR_FAILURE/
  );

  Assert.ok(!msc.indexExists("sample.test_ind"));
  msc.executeSimpleSQL("CREATE INDEX sample.test_ind ON test (name)");
  Assert.ok(msc.indexExists("sample.test_ind"));

  msc.executeSimpleSQL("DETACH DATABASE sample");
  msc2.close();
  try {
    file.remove(false);
  } catch (e) {
    // Do nothing.
  }
});

add_task(async function test_lastInsertRowID() {
  var msc = getOpenedDatabase();
  msc.executeSimpleSQL("INSERT INTO test (name) VALUES ('foo')");
  Assert.equal(1, msc.lastInsertRowID);
});

add_task(async function test_transactionInProgress_no() {
  var msc = getOpenedDatabase();
  Assert.ok(!msc.transactionInProgress);
});

add_task(async function test_transactionInProgress_yes() {
  var msc = getOpenedDatabase();
  msc.beginTransaction();
  Assert.ok(msc.transactionInProgress);
  msc.commitTransaction();
  Assert.ok(!msc.transactionInProgress);

  msc.beginTransaction();
  Assert.ok(msc.transactionInProgress);
  msc.rollbackTransaction();
  Assert.ok(!msc.transactionInProgress);
});

add_task(async function test_commitTransaction_no_transaction() {
  var msc = getOpenedDatabase();
  Assert.ok(!msc.transactionInProgress);
  Assert.throws(() => msc.commitTransaction(), /NS_ERROR_UNEXPECTED/);
});

add_task(async function test_rollbackTransaction_no_transaction() {
  var msc = getOpenedDatabase();
  Assert.ok(!msc.transactionInProgress);
  Assert.throws(() => msc.rollbackTransaction(), /NS_ERROR_UNEXPECTED/);
});

add_task(async function test_get_schemaVersion_not_set() {
  Assert.equal(0, getOpenedDatabase().schemaVersion);
});

add_task(async function test_set_schemaVersion() {
  var msc = getOpenedDatabase();
  const version = 1;
  msc.schemaVersion = version;
  Assert.equal(version, msc.schemaVersion);
});

add_task(async function test_set_schemaVersion_same() {
  var msc = getOpenedDatabase();
  const version = 1;
  msc.schemaVersion = version; // should still work ok
  Assert.equal(version, msc.schemaVersion);
});

add_task(async function test_set_schemaVersion_negative() {
  var msc = getOpenedDatabase();
  const version = -1;
  msc.schemaVersion = version;
  Assert.equal(version, msc.schemaVersion);
});

add_task(async function test_createTable() {
  var temp = getTestDB().parent;
  temp.append("test_db_table");
  try {
    var con = Services.storage.openDatabase(temp);
    con.createTable("a", "");
  } catch (e) {
    if (temp.exists()) {
      try {
        temp.remove(false);
      } catch (e2) {
        // Do nothing.
      }
    }
    Assert.ok(
      e.result == Cr.NS_ERROR_NOT_INITIALIZED || e.result == Cr.NS_ERROR_FAILURE
    );
  } finally {
    if (con) {
      con.close();
    }
  }
});

add_task(async function test_defaultSynchronousAtNormal() {
  getOpenedDatabase();
  var stmt = createStatement("PRAGMA synchronous;");
  try {
    stmt.executeStep();
    Assert.equal(1, stmt.getInt32(0));
  } finally {
    stmt.reset();
    stmt.finalize();
  }
});

// must be ran before executeAsync tests
add_task(async function test_close_does_not_spin_event_loop() {
  // We want to make sure that the event loop on the calling thread does not
  // spin when close is called.
  let event = {
    ran: false,
    run() {
      this.ran = true;
    },
  };

  // Post the event before we call close, so it would run if the event loop was
  // spun during close.
  Services.tm.dispatchToMainThread(event);

  // Sanity check, then close the database.  Afterwards, we should not have ran!
  Assert.ok(!event.ran);
  getOpenedDatabase().close();
  Assert.ok(!event.ran);

  // Reset gDBConn so that later tests will get a new connection object.
  gDBConn = null;
});

add_task(
  async function test_asyncClose_succeeds_with_finalized_async_statement() {
    // XXX this test isn't perfect since we can't totally control when events will
    //     run.  If this paticular function fails randomly, it means we have a
    //     real bug.

    // We want to make sure we create a cached async statement to make sure that
    // when we finalize our statement, we end up finalizing the async one too so
    // close will succeed.
    let stmt = createStatement("SELECT * FROM test");
    stmt.executeAsync();
    stmt.finalize();

    await asyncClose(getOpenedDatabase());
    // Reset gDBConn so that later tests will get a new connection object.
    gDBConn = null;
  }
);

// Would assert on debug builds.
if (!AppConstants.DEBUG) {
  add_task(async function test_close_then_release_statement() {
    // Testing the behavior in presence of a bad client that finalizes
    // statements after the database has been closed (typically by
    // letting the gc finalize the statement).
    let db = getOpenedDatabase();
    let stmt = createStatement(
      "SELECT * FROM test -- test_close_then_release_statement"
    );
    db.close();
    stmt.finalize(); // Finalize too late - this should not crash

    // Reset gDBConn so that later tests will get a new connection object.
    gDBConn = null;
  });

  add_task(async function test_asyncClose_then_release_statement() {
    // Testing the behavior in presence of a bad client that finalizes
    // statements after the database has been async closed (typically by
    // letting the gc finalize the statement).
    let db = getOpenedDatabase();
    let stmt = createStatement(
      "SELECT * FROM test -- test_asyncClose_then_release_statement"
    );
    await asyncClose(db);
    stmt.finalize(); // Finalize too late - this should not crash

    // Reset gDBConn so that later tests will get a new connection object.
    gDBConn = null;
  });
}

// In debug builds this would cause a fatal assertion.
if (!AppConstants.DEBUG) {
  add_task(async function test_close_fails_with_async_statement_ran() {
    let stmt = createStatement("SELECT * FROM test");
    stmt.executeAsync();
    stmt.finalize();

    let db = getOpenedDatabase();
    Assert.throws(() => db.close(), /NS_ERROR_UNEXPECTED/);
    // Reset gDBConn so that later tests will get a new connection object.
    gDBConn = null;
  });
}

add_task(async function test_clone_optional_param() {
  let db1 = Services.storage.openUnsharedDatabase(getTestDB());
  let db2 = db1.clone();
  Assert.ok(db2.connectionReady);

  // A write statement should not fail here.
  let stmt = db2.createStatement("INSERT INTO test (name) VALUES (:name)");
  stmt.params.name = "dwitte";
  stmt.execute();
  stmt.finalize();

  // And a read statement should succeed.
  stmt = db2.createStatement("SELECT * FROM test");
  Assert.ok(stmt.executeStep());
  stmt.finalize();

  // Additionally check that it is a connection on the same database.
  Assert.ok(db1.databaseFile.equals(db2.databaseFile));

  db1.close();
  db2.close();
});

async function standardAsyncTest(promisedDB, name, shouldInit = false) {
  info("Performing standard async test " + name);

  let adb = await promisedDB;
  Assert.ok(adb instanceof Ci.mozIStorageAsyncConnection);
  Assert.ok(adb instanceof Ci.mozIStorageConnection);

  if (shouldInit) {
    let stmt = adb.createAsyncStatement("CREATE TABLE test(name TEXT)");
    await executeAsync(stmt);
    stmt.finalize();
  }

  // Generate a name to insert and fetch back
  name = "worker bee " + Math.random() + " (" + name + ")";

  let stmt = adb.createAsyncStatement("INSERT INTO test (name) VALUES (:name)");
  stmt.params.name = name;
  let result = await executeAsync(stmt);
  info("Request complete");
  stmt.finalize();
  Assert.ok(Components.isSuccessCode(result));
  info("Extracting data");
  stmt = adb.createAsyncStatement("SELECT * FROM test");
  let found = false;
  await executeAsync(stmt, function (results) {
    info("Data has been extracted");
    for (
      let row = results.getNextRow();
      row != null;
      row = results.getNextRow()
    ) {
      if (row.getResultByName("name") == name) {
        found = true;
        break;
      }
    }
  });
  Assert.ok(found);
  stmt.finalize();
  await asyncClose(adb);

  info("Standard async test " + name + " complete");
}

add_task(async function test_open_async() {
  await standardAsyncTest(openAsyncDatabase(getTestDB(), null), "default");
  await standardAsyncTest(openAsyncDatabase(getTestDB()), "no optional arg");
  await standardAsyncTest(
    openAsyncDatabase(getTestDB(), { shared: false, interruptible: true }),
    "non-default options"
  );
  await standardAsyncTest(
    openAsyncDatabase("memory"),
    "in-memory database",
    true
  );
  await standardAsyncTest(
    openAsyncDatabase("memory", { shared: false }),
    "in-memory database and options",
    true
  );

  info("Testing async opening with readonly option");
  const impliedReadOnlyOption = { ignoreLockingMode: true };

  let raised = false;
  let adb = await openAsyncDatabase(getTestDB(), impliedReadOnlyOption);
  let stmt = adb.createAsyncStatement("CREATE TABLE test(name TEXT)");
  try {
    await executeAsync(stmt); // This should throw
  } catch (e) {
    raised = true;
  } finally {
    if (stmt) {
      stmt.finalize();
    }
    if (adb) {
      await asyncClose(adb);
    }
  }

  Assert.ok(raised);
});

add_task(async function test_async_open_with_shared_cache() {
  info("Testing that opening with a shared cache doesn't break stuff");
  let adb = await openAsyncDatabase(getTestDB(), { shared: true });

  let stmt = adb.createAsyncStatement("INSERT INTO test (name) VALUES (:name)");
  stmt.params.name = "clockworker";
  let result = await executeAsync(stmt);
  info("Request complete");
  stmt.finalize();
  Assert.ok(Components.isSuccessCode(result));
  info("Extracting data");
  stmt = adb.createAsyncStatement("SELECT * FROM test");
  let found = false;
  await executeAsync(stmt, function (results) {
    info("Data has been extracted");
    for (
      let row = results.getNextRow();
      row != null;
      row = results.getNextRow()
    ) {
      if (row.getResultByName("name") == "clockworker") {
        found = true;
        break;
      }
    }
  });
  Assert.ok(found);
  stmt.finalize();
  await asyncClose(adb);
});

add_task(async function test_clone_trivial_async() {
  info("Open connection");
  let db = Services.storage.openDatabase(getTestDB());
  Assert.ok(db instanceof Ci.mozIStorageAsyncConnection);
  info("AsyncClone connection");
  let clone = await asyncClone(db, true);
  Assert.ok(clone instanceof Ci.mozIStorageAsyncConnection);
  Assert.ok(clone instanceof Ci.mozIStorageConnection);
  info("Close connection");
  await asyncClose(db);
  info("Close clone");
  await asyncClose(clone);
});

add_task(async function test_clone_no_optional_param_async() {
  "use strict";
  info("Testing async cloning");
  let adb1 = await openAsyncDatabase(getTestDB(), null);
  Assert.ok(adb1 instanceof Ci.mozIStorageAsyncConnection);
  Assert.ok(adb1 instanceof Ci.mozIStorageConnection);

  info("Cloning database");

  let adb2 = await asyncClone(adb1);
  info(
    "Testing that the cloned db is a mozIStorageAsyncConnection " +
      "and not a mozIStorageConnection"
  );
  Assert.ok(adb2 instanceof Ci.mozIStorageAsyncConnection);
  Assert.ok(adb2 instanceof Ci.mozIStorageConnection);

  info("Inserting data into source db");
  let stmt = adb1.createAsyncStatement(
    "INSERT INTO test (name) VALUES (:name)"
  );

  stmt.params.name = "yoric";
  let result = await executeAsync(stmt);
  info("Request complete");
  stmt.finalize();
  Assert.ok(Components.isSuccessCode(result));
  info("Extracting data from clone db");
  stmt = adb2.createAsyncStatement("SELECT * FROM test");
  let found = false;
  await executeAsync(stmt, function (results) {
    info("Data has been extracted");
    for (
      let row = results.getNextRow();
      row != null;
      row = results.getNextRow()
    ) {
      if (row.getResultByName("name") == "yoric") {
        found = true;
        break;
      }
    }
  });
  Assert.ok(found);
  stmt.finalize();
  info("Closing databases");
  await asyncClose(adb2);
  info("First db closed");

  await asyncClose(adb1);
  info("Second db closed");
});

add_task(async function test_clone_readonly() {
  let db1 = Services.storage.openUnsharedDatabase(getTestDB());
  let db2 = db1.clone(true);
  Assert.ok(db2.connectionReady);

  // A write statement should fail here.
  let stmt = db2.createStatement("INSERT INTO test (name) VALUES (:name)");
  stmt.params.name = "reed";
  expectError(Cr.NS_ERROR_FILE_READ_ONLY, () => stmt.execute());
  stmt.finalize();

  // And a read statement should succeed.
  stmt = db2.createStatement("SELECT * FROM test");
  Assert.ok(stmt.executeStep());
  stmt.finalize();

  db1.close();
  db2.close();
});

add_task(async function test_clone_shared_readonly() {
  let db1 = Services.storage.openDatabase(getTestDB());
  let db2 = db1.clone(true);
  Assert.ok(db2.connectionReady);

  let stmt = db2.createStatement("INSERT INTO test (name) VALUES (:name)");
  stmt.params.name = "parker";
  // TODO currently SQLite does not actually work correctly here.  The behavior
  //      we want is commented out, and the current behavior is being tested
  //      for.  Our IDL comments will have to be updated when this starts to
  //      work again.
  stmt.execute();
  // expectError(Components.results.NS_ERROR_FILE_READ_ONLY, () => stmt.execute());
  stmt.finalize();

  // And a read statement should succeed.
  stmt = db2.createStatement("SELECT * FROM test");
  Assert.ok(stmt.executeStep());
  stmt.finalize();

  db1.close();
  db2.close();
});

add_task(async function test_close_clone_fails() {
  let calls = ["openDatabase", "openUnsharedDatabase"];
  calls.forEach(function (methodName) {
    let db = Services.storage[methodName](getTestDB());
    db.close();
    expectError(Cr.NS_ERROR_NOT_INITIALIZED, () => db.clone());
  });
});

add_task(async function test_clone_copies_functions() {
  const FUNC_NAME = "test_func";
  let calls = ["openDatabase", "openUnsharedDatabase"];
  let functionMethods = ["createFunction"];
  calls.forEach(function (methodName) {
    [true, false].forEach(function (readOnly) {
      functionMethods.forEach(function (functionMethod) {
        let db1 = Services.storage[methodName](getTestDB());
        // Create a function for db1.
        db1[functionMethod](FUNC_NAME, 1, {
          onFunctionCall: () => 0,
          onStep: () => 0,
          onFinal: () => 0,
        });

        // Clone it, and make sure the function exists still.
        let db2 = db1.clone(readOnly);
        // Note: this would fail if the function did not exist.
        let stmt = db2.createStatement(
          "SELECT " + FUNC_NAME + "(id) FROM test"
        );
        stmt.finalize();
        db1.close();
        db2.close();
      });
    });
  });
});

add_task(async function test_clone_copies_overridden_functions() {
  const FUNC_NAME = "lower";
  function test_func() {
    this.called = false;
  }
  test_func.prototype = {
    onFunctionCall() {
      this.called = true;
    },
    onStep() {
      this.called = true;
    },
    onFinal: () => 0,
  };

  let calls = ["openDatabase", "openUnsharedDatabase"];
  let functionMethods = ["createFunction"];
  calls.forEach(function (methodName) {
    [true, false].forEach(function (readOnly) {
      functionMethods.forEach(function (functionMethod) {
        let db1 = Services.storage[methodName](getTestDB());
        // Create a function for db1.
        let func = new test_func();
        db1[functionMethod](FUNC_NAME, 1, func);
        Assert.ok(!func.called);

        // Clone it, and make sure the function gets called.
        let db2 = db1.clone(readOnly);
        let stmt = db2.createStatement(
          "SELECT " + FUNC_NAME + "(id) FROM test"
        );
        stmt.executeStep();
        Assert.ok(func.called);
        stmt.finalize();
        db1.close();
        db2.close();
      });
    });
  });
});

add_task(async function test_clone_copies_pragmas() {
  const PRAGMAS = [
    { name: "cache_size", value: 500, copied: true },
    { name: "temp_store", value: 2, copied: true },
    { name: "foreign_keys", value: 1, copied: true },
    { name: "journal_size_limit", value: 524288, copied: true },
    { name: "synchronous", value: 2, copied: true },
    { name: "wal_autocheckpoint", value: 16, copied: true },
    { name: "busy_timeout", value: 50, copied: false },
    { name: "ignore_check_constraints", value: 1, copied: false },
  ];

  let db1 = Services.storage.openUnsharedDatabase(getTestDB());

  // Sanity check initial values are different from enforced ones.
  PRAGMAS.forEach(function (pragma) {
    let stmt = db1.createStatement("PRAGMA " + pragma.name);
    Assert.ok(stmt.executeStep());
    Assert.notEqual(pragma.value, stmt.getInt32(0));
    stmt.finalize();
  });
  // Execute pragmas.
  PRAGMAS.forEach(function (pragma) {
    db1.executeSimpleSQL("PRAGMA " + pragma.name + " = " + pragma.value);
  });

  let db2 = db1.clone();
  Assert.ok(db2.connectionReady);

  // Check cloned connection inherited pragma values.
  PRAGMAS.forEach(function (pragma) {
    let stmt = db2.createStatement("PRAGMA " + pragma.name);
    Assert.ok(stmt.executeStep());
    let validate = pragma.copied ? "equal" : "notEqual";
    Assert[validate](pragma.value, stmt.getInt32(0));
    stmt.finalize();
  });

  db1.close();
  db2.close();
});

add_task(async function test_readonly_clone_copies_pragmas() {
  const PRAGMAS = [
    { name: "cache_size", value: 500, copied: true },
    { name: "temp_store", value: 2, copied: true },
    { name: "foreign_keys", value: 1, copied: false },
    { name: "journal_size_limit", value: 524288, copied: false },
    { name: "synchronous", value: 2, copied: false },
    { name: "wal_autocheckpoint", value: 16, copied: false },
    { name: "busy_timeout", value: 50, copied: false },
    { name: "ignore_check_constraints", value: 1, copied: false },
  ];

  let db1 = Services.storage.openUnsharedDatabase(getTestDB());

  // Sanity check initial values are different from enforced ones.
  PRAGMAS.forEach(function (pragma) {
    let stmt = db1.createStatement("PRAGMA " + pragma.name);
    Assert.ok(stmt.executeStep());
    Assert.notEqual(pragma.value, stmt.getInt32(0));
    stmt.finalize();
  });
  // Execute pragmas.
  PRAGMAS.forEach(function (pragma) {
    db1.executeSimpleSQL("PRAGMA " + pragma.name + " = " + pragma.value);
  });

  let db2 = db1.clone(true);
  Assert.ok(db2.connectionReady);

  // Check cloned connection inherited pragma values.
  PRAGMAS.forEach(function (pragma) {
    let stmt = db2.createStatement("PRAGMA " + pragma.name);
    Assert.ok(stmt.executeStep());
    let validate = pragma.copied ? "equal" : "notEqual";
    Assert[validate](pragma.value, stmt.getInt32(0));
    stmt.finalize();
  });

  db1.close();
  db2.close();
});

add_task(async function test_clone_attach_database() {
  let db1 = Services.storage.openUnsharedDatabase(getTestDB());

  let c = 0;
  function attachDB(conn, name) {
    let file = Services.dirsvc.get("ProfD", Ci.nsIFile);
    file.append("test_storage_" + ++c + ".sqlite");
    let db = Services.storage.openUnsharedDatabase(file);
    conn.executeSimpleSQL(
      `ATTACH DATABASE '${db.databaseFile.path}' AS ${name}`
    );
    db.executeSimpleSQL(`CREATE TABLE test_${name}(name TEXT);`);
    db.close();
  }
  attachDB(db1, "attached_1");
  attachDB(db1, "attached_2");
  db1.executeSimpleSQL(`
    CREATE TEMP TRIGGER test_temp_afterinsert_trigger
    AFTER DELETE ON test_attached_1 FOR EACH ROW
    BEGIN
      INSERT INTO test(name) VALUES(OLD.name);
    END`);

  // These should not throw.
  let stmt = db1.createStatement("SELECT * FROM attached_1.sqlite_master");
  stmt.finalize();
  stmt = db1.createStatement("SELECT * FROM attached_2.sqlite_master");
  stmt.finalize();
  db1.executeSimpleSQL("INSERT INTO test_attached_1(name) VALUES('asuth')");
  db1.executeSimpleSQL("DELETE FROM test_attached_1");
  Assert.ok(fetchAllNames(db1).includes("asuth"));

  // R/W clone.
  let db2 = db1.clone();
  Assert.ok(db2.connectionReady);

  // These should not throw.
  stmt = db2.createStatement("SELECT * FROM attached_1.sqlite_master");
  stmt.finalize();
  stmt = db2.createStatement("SELECT * FROM attached_2.sqlite_master");
  stmt.finalize();
  db2.executeSimpleSQL("INSERT INTO test_attached_1(name) VALUES('past')");
  db2.executeSimpleSQL("DELETE FROM test_attached_1");
  let newNames = fetchAllNames(db2);
  Assert.ok(newNames.includes("past"));
  Assert.deepEqual(fetchAllNames(db1), newNames);

  // R/O clone.
  let db3 = db1.clone(true);
  Assert.ok(db3.connectionReady);

  // These should not throw.
  stmt = db3.createStatement("SELECT * FROM attached_1.sqlite_master");
  stmt.finalize();
  stmt = db3.createStatement("SELECT * FROM attached_2.sqlite_master");
  stmt.finalize();

  db1.close();
  db2.close();
  db3.close();
});

add_task(async function test_async_clone_with_temp_trigger_and_table() {
  info("Open connection");
  let db = Services.storage.openDatabase(getTestDB());
  Assert.ok(db instanceof Ci.mozIStorageAsyncConnection);

  info("Set up tables on original connection");
  let createQueries = [
    `CREATE TEMP TABLE test_temp(name TEXT)`,
    `CREATE INDEX test_temp_idx ON test_temp(name)`,
    `CREATE TEMP TRIGGER test_temp_afterdelete_trigger
     AFTER DELETE ON test_temp FOR EACH ROW
     BEGIN
       INSERT INTO test(name) VALUES(OLD.name);
     END`,
  ];
  for (let query of createQueries) {
    let stmt = db.createAsyncStatement(query);
    await executeAsync(stmt);
    stmt.finalize();
  }

  info("Create read-write clone with temp tables");
  let readWriteClone = await asyncClone(db, false);
  Assert.ok(readWriteClone instanceof Ci.mozIStorageAsyncConnection);

  info("Insert into temp table on read-write clone");
  let insertStmt = readWriteClone.createAsyncStatement(`
    INSERT INTO test_temp(name) VALUES('mak'), ('standard8'), ('markh')`);
  await executeAsync(insertStmt);
  insertStmt.finalize();

  info("Fire temp trigger on read-write clone");
  let deleteStmt = readWriteClone.createAsyncStatement(`
    DELETE FROM test_temp`);
  await executeAsync(deleteStmt);
  deleteStmt.finalize();

  info("Read from original connection");
  let names = fetchAllNames(db);
  Assert.ok(names.includes("mak"));
  Assert.ok(names.includes("standard8"));
  Assert.ok(names.includes("markh"));

  info("Create read-only clone");
  let readOnlyClone = await asyncClone(db, true);
  Assert.ok(readOnlyClone instanceof Ci.mozIStorageAsyncConnection);

  info("Read-only clone shouldn't have temp entities");
  let badStmt = readOnlyClone.createAsyncStatement(`SELECT 1 FROM test_temp`);
  await Assert.rejects(executeAsync(badStmt), Ci.mozIStorageError);
  badStmt.finalize();

  info("Clean up");
  for (let conn of [db, readWriteClone, readOnlyClone]) {
    await asyncClose(conn);
  }
});

add_task(async function test_sync_clone_in_transaction() {
  info("Open connection");
  let db = Services.storage.openDatabase(getTestDB());
  Assert.ok(db instanceof Ci.mozIStorageAsyncConnection);

  info("Begin transaction on main connection");
  db.beginTransaction();

  info("Create temp table and trigger in transaction");
  let createQueries = [
    `CREATE TEMP TABLE test_temp(name TEXT)`,
    `CREATE TEMP TRIGGER test_temp_afterdelete_trigger
     AFTER DELETE ON test_temp FOR EACH ROW
     BEGIN
       INSERT INTO test(name) VALUES(OLD.name);
     END`,
  ];
  for (let query of createQueries) {
    db.executeSimpleSQL(query);
  }

  info("Clone main connection while transaction is in progress");
  let clone = db.clone(/* aReadOnly */ false);

  // Dropping the table also drops `test_temp_afterdelete_trigger`.
  info("Drop temp table on main connection");
  db.executeSimpleSQL(`DROP TABLE test_temp`);

  info("Commit transaction");
  db.commitTransaction();

  info("Clone connection should still have temp entities");
  let readTempStmt = clone.createStatement(`SELECT 1 FROM test_temp`);
  readTempStmt.execute();
  readTempStmt.finalize();

  info("Clean up");

  db.close();
  clone.close();
});

add_task(async function test_sync_clone_with_function() {
  info("Open connection");
  let db = Services.storage.openDatabase(getTestDB());
  Assert.ok(db instanceof Ci.mozIStorageAsyncConnection);

  info("Create SQL function");
  function storeLastInsertedNameFunc() {
    this.name = null;
  }
  storeLastInsertedNameFunc.prototype = {
    onFunctionCall(args) {
      this.name = args.getUTF8String(0);
    },
  };
  let func = new storeLastInsertedNameFunc();
  db.createFunction("store_last_inserted_name", 1, func);

  info("Create temp trigger on main connection");
  db.executeSimpleSQL(`
    CREATE TEMP TRIGGER test_afterinsert_trigger
    AFTER INSERT ON test FOR EACH ROW
    BEGIN
      SELECT store_last_inserted_name(NEW.name);
    END`);

  info("Clone main connection");
  let clone = db.clone(/* aReadOnly */ false);

  info("Write to clone");
  clone.executeSimpleSQL(`INSERT INTO test(name) VALUES('kit')`);

  Assert.equal(func.name, "kit");

  info("Clean up");
  db.close();
  clone.close();
});

add_task(async function test_defaultTransactionType() {
  info("Open connection");
  let db = Services.storage.openDatabase(getTestDB());
  Assert.ok(db instanceof Ci.mozIStorageAsyncConnection);

  info("Verify default transaction type");
  Assert.equal(
    db.defaultTransactionType,
    Ci.mozIStorageConnection.TRANSACTION_DEFERRED
  );

  info("Test other transaction types");
  for (let type of [
    Ci.mozIStorageConnection.TRANSACTION_IMMEDIATE,
    Ci.mozIStorageConnection.TRANSACTION_EXCLUSIVE,
  ]) {
    db.defaultTransactionType = type;
    Assert.equal(db.defaultTransactionType, type);
  }

  info("Should reject unknown transaction types");
  Assert.throws(
    () =>
      (db.defaultTransactionType =
        Ci.mozIStorageConnection.TRANSACTION_DEFAULT),
    /NS_ERROR_ILLEGAL_VALUE/
  );

  db.defaultTransactionType = Ci.mozIStorageConnection.TRANSACTION_IMMEDIATE;

  info("Clone should inherit default transaction type");
  let clone = await asyncClone(db, true);
  Assert.ok(clone instanceof Ci.mozIStorageAsyncConnection);
  Assert.equal(
    clone.defaultTransactionType,
    Ci.mozIStorageConnection.TRANSACTION_IMMEDIATE
  );

  info("Begin immediate transaction on main connection");
  db.beginTransaction();

  info("Queue immediate transaction on clone");
  let stmts = [
    clone.createAsyncStatement(`BEGIN IMMEDIATE TRANSACTION`),
    clone.createAsyncStatement(`DELETE FROM test WHERE name = 'new'`),
    clone.createAsyncStatement(`COMMIT`),
  ];
  let promiseStmtsRan = stmts.map(stmt => executeAsync(stmt));

  info("Commit immediate transaction on main connection");
  db.executeSimpleSQL(`INSERT INTO test(name) VALUES('new')`);
  db.commitTransaction();

  info("Wait for transaction to succeed on clone");
  await Promise.all(promiseStmtsRan);

  info("Clean up");
  for (let stmt of stmts) {
    stmt.finalize();
  }
  await asyncClose(clone);
  await asyncClose(db);
});

add_task(async function test_variableLimit() {
  info("Open connection");
  let db = Services.storage.openDatabase(getTestDB());
  Assert.equal(db.variableLimit, 32766, "Should return default limit");
  db.variableLimit = 999;
  Assert.equal(db.variableLimit, 999, "Should return the set limit");
  db.variableLimit = 33000;
  Assert.equal(db.variableLimit, 32766, "Should silently truncate");
  await asyncClose(db);
});

add_task(async function test_getInterface() {
  let db = getOpenedDatabase();
  let target = db
    .QueryInterface(Ci.nsIInterfaceRequestor)
    .getInterface(Ci.nsIEventTarget);
  // Just check that target is non-null.  Other tests will ensure that it has
  // the correct value.
  Assert.notEqual(target, null);

  await asyncClose(db);
  gDBConn = null;
});
