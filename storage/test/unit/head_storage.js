/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);
var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  FileUtils: "resource://gre/modules/FileUtils.sys.mjs",
  Sqlite: "resource://gre/modules/Sqlite.sys.mjs",
  TelemetryTestUtils: "resource://testing-common/TelemetryTestUtils.sys.mjs",
  TestUtils: "resource://testing-common/TestUtils.sys.mjs",
});

const TELEMETRY_VALUES = {
  success: 0,
  failure: 1,
  access: 2,
  diskio: 3,
  corrupt: 4,
  busy: 5,
  misuse: 6,
  diskspace: 7,
};

do_get_profile();
var gDBConn = null;

const TEST_DB_NAME = "test_storage.sqlite";
function getTestDB() {
  var db = Services.dirsvc.get("ProfD", Ci.nsIFile);
  db.append(TEST_DB_NAME);
  return db;
}

/**
 * Obtains a fake (non-SQLite format) database to test against.
 */
function getFakeDB() {
  return do_get_file("fakeDB.sqlite");
}

/**
 * Delete the test database file.
 */
function deleteTestDB() {
  print("*** Storage Tests: Trying to remove file!");
  var dbFile = getTestDB();
  if (dbFile.exists()) {
    try {
      dbFile.remove(false);
    } catch (e) {
      /* stupid windows box */
    }
  }
}

function cleanup() {
  // close the connection
  print("*** Storage Tests: Trying to close!");
  getOpenedDatabase().close();

  // we need to null out the database variable to get a new connection the next
  // time getOpenedDatabase is called
  gDBConn = null;

  // removing test db
  deleteTestDB();
}

/**
 * Use asyncClose to cleanup a connection.  Synchronous by means of internally
 * spinning an event loop.
 */
function asyncCleanup() {
  let closed = false;

  // close the connection
  print("*** Storage Tests: Trying to asyncClose!");
  getOpenedDatabase().asyncClose(function () {
    closed = true;
  });

  let tm = Cc["@mozilla.org/thread-manager;1"].getService();
  tm.spinEventLoopUntil("Test(head_storage.js:asyncCleanup)", () => closed);

  // we need to null out the database variable to get a new connection the next
  // time getOpenedDatabase is called
  gDBConn = null;

  // removing test db
  deleteTestDB();
}

/**
 * Get a connection to the test database.  Creates and caches the connection
 * if necessary, otherwise reuses the existing cached connection. This
 * connection shares its cache.
 *
 * @returns {mozIStorageConnection}
 */
function getOpenedDatabase(connectionFlags = 0) {
  if (!gDBConn) {
    gDBConn = Services.storage.openDatabase(getTestDB(), connectionFlags);
  }
  return gDBConn;
}

/**
 * Get a connection to the test database.  Creates and caches the connection
 * if necessary, otherwise reuses the existing cached connection. This
 * connection doesn't share its cache.
 *
 * @returns {mozIStorageConnection}
 */
function getOpenedUnsharedDatabase() {
  if (!gDBConn) {
    gDBConn = Services.storage.openUnsharedDatabase(getTestDB());
  }
  return gDBConn;
}

/**
 * Obtains a specific database to use.
 *
 * @param aFile
 *        The nsIFile representing the db file to open.
 * @returns {mozIStorageConnection}
 */
function getDatabase(aFile) {
  return Services.storage.openDatabase(aFile);
}

function createStatement(aSQL) {
  return getOpenedDatabase().createStatement(aSQL);
}

/**
 * Creates an asynchronous SQL statement.
 *
 * @param aSQL
 *        The SQL to parse into a statement.
 * @returns {mozIStorageAsyncStatement}
 */
function createAsyncStatement(aSQL) {
  return getOpenedDatabase().createAsyncStatement(aSQL);
}

/**
 * Invoke the given function and assert that it throws an exception expressing
 * the provided error code in its 'result' attribute.  JS function expressions
 * can be used to do this concisely.
 *
 * Example:
 *  expectError(Cr.NS_ERROR_INVALID_ARG, () => explodingFunction());
 *
 * @param aErrorCode
 *        The error code to expect from invocation of aFunction.
 * @param aFunction
 *        The function to invoke and expect an XPCOM-style error from.
 */
function expectError(aErrorCode, aFunction) {
  let exceptionCaught = false;
  try {
    aFunction();
  } catch (e) {
    if (e.result != aErrorCode) {
      do_throw(
        "Got an exception, but the result code was not the expected " +
          "one.  Expected " +
          aErrorCode +
          ", got " +
          e.result
      );
    }
    exceptionCaught = true;
  }
  if (!exceptionCaught) {
    do_throw(aFunction + " should have thrown an exception but did not!");
  }
}

/**
 * Run a query synchronously and verify that we get back the expected results.
 *
 * @param aSQLString
 *        The SQL string for the query.
 * @param aBind
 *        The value to bind at index 0.
 * @param aResults
 *        A list of the expected values returned in the sole result row.
 *        Express blobs as lists.
 */
function verifyQuery(aSQLString, aBind, aResults) {
  let stmt = getOpenedDatabase().createStatement(aSQLString);
  stmt.bindByIndex(0, aBind);
  try {
    Assert.ok(stmt.executeStep());
    let nCols = stmt.numEntries;
    if (aResults.length != nCols) {
      do_throw(
        "Expected " +
          aResults.length +
          " columns in result but " +
          "there are only " +
          aResults.length +
          "!"
      );
    }
    for (let iCol = 0; iCol < nCols; iCol++) {
      let expectedVal = aResults[iCol];
      let valType = stmt.getTypeOfIndex(iCol);
      if (expectedVal === null) {
        Assert.equal(stmt.VALUE_TYPE_NULL, valType);
        Assert.ok(stmt.getIsNull(iCol));
      } else if (typeof expectedVal == "number") {
        if (Math.floor(expectedVal) == expectedVal) {
          Assert.equal(stmt.VALUE_TYPE_INTEGER, valType);
          Assert.equal(expectedVal, stmt.getInt32(iCol));
        } else {
          Assert.equal(stmt.VALUE_TYPE_FLOAT, valType);
          Assert.equal(expectedVal, stmt.getDouble(iCol));
        }
      } else if (typeof expectedVal == "string") {
        Assert.equal(stmt.VALUE_TYPE_TEXT, valType);
        Assert.equal(expectedVal, stmt.getUTF8String(iCol));
      } else {
        // blob
        Assert.equal(stmt.VALUE_TYPE_BLOB, valType);
        let count = { value: 0 },
          blob = { value: null };
        stmt.getBlob(iCol, count, blob);
        Assert.equal(count.value, expectedVal.length);
        for (let i = 0; i < count.value; i++) {
          Assert.equal(expectedVal[i], blob.value[i]);
        }
      }
    }
  } finally {
    stmt.finalize();
  }
}

/**
 * Return the number of rows in the able with the given name using a synchronous
 * query.
 *
 * @param aTableName
 *        The name of the table.
 * @return The number of rows.
 */
function getTableRowCount(aTableName) {
  var currentRows = 0;
  var countStmt = getOpenedDatabase().createStatement(
    "SELECT COUNT(1) AS count FROM " + aTableName
  );
  try {
    Assert.ok(countStmt.executeStep());
    currentRows = countStmt.row.count;
  } finally {
    countStmt.finalize();
  }
  return currentRows;
}

// Promise-Returning Functions

function asyncClone(db, readOnly) {
  return new Promise((resolve, reject) => {
    db.asyncClone(readOnly, function (status, db2) {
      if (Components.isSuccessCode(status)) {
        resolve(db2.QueryInterface(Ci.mozIStorageAsyncConnection));
      } else {
        reject(status);
      }
    });
  });
}

function asyncClose(db) {
  return new Promise((resolve, reject) => {
    db.asyncClose(function (status) {
      if (Components.isSuccessCode(status)) {
        resolve();
      } else {
        reject(status);
      }
    });
  });
}

function mapOptionsToFlags(aOptions, aMapping) {
  let result = aMapping.default;
  Object.entries(aOptions || {}).forEach(([optionName, isTrue]) => {
    if (aMapping.hasOwnProperty(optionName) && isTrue) {
      result |= aMapping[optionName];
    }
  });
  return result;
}

function getOpenFlagsMap() {
  return {
    default: Ci.mozIStorageService.OPEN_DEFAULT,
    shared: Ci.mozIStorageService.OPEN_SHARED,
    readOnly: Ci.mozIStorageService.OPEN_READONLY,
    ignoreLockingMode: Ci.mozIStorageService.OPEN_IGNORE_LOCKING_MODE,
  };
}

function getConnectionFlagsMap() {
  return {
    default: Ci.mozIStorageService.CONNECTION_DEFAULT,
    interruptible: Ci.mozIStorageService.CONNECTION_INTERRUPTIBLE,
  };
}

function openAsyncDatabase(file, options) {
  return new Promise((resolve, reject) => {
    const openFlags = mapOptionsToFlags(options, getOpenFlagsMap());
    const connectionFlags = mapOptionsToFlags(options, getConnectionFlagsMap());

    Services.storage.openAsyncDatabase(
      file,
      openFlags,
      connectionFlags,
      function (status, db) {
        if (Components.isSuccessCode(status)) {
          resolve(db.QueryInterface(Ci.mozIStorageAsyncConnection));
        } else {
          reject(status);
        }
      }
    );
  });
}

function executeAsync(statement, onResult) {
  return new Promise((resolve, reject) => {
    statement.executeAsync({
      handleError(error) {
        reject(error);
      },
      handleResult(result) {
        if (onResult) {
          onResult(result);
        }
      },
      handleCompletion(result) {
        resolve(result);
      },
    });
  });
}

function executeMultipleStatementsAsync(db, statements, onResult) {
  return new Promise((resolve, reject) => {
    db.executeAsync(statements, {
      handleError(error) {
        reject(error);
      },
      handleResult(result) {
        if (onResult) {
          onResult(result);
        }
      },
      handleCompletion(result) {
        resolve(result);
      },
    });
  });
}

function executeSimpleSQLAsync(db, query, onResult) {
  return new Promise((resolve, reject) => {
    db.executeSimpleSQLAsync(query, {
      handleError(error) {
        reject(error);
      },
      handleResult(result) {
        if (onResult) {
          onResult(result);
        } else {
          do_throw("No results were expected");
        }
      },
      handleCompletion(result) {
        resolve(result);
      },
    });
  });
}

cleanup();
