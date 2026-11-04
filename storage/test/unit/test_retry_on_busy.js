/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* eslint-disable mozilla/no-arbitrary-setTimeout */

const { clearInterval, setInterval, setTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

function getProfileFile(name) {
  let file = do_get_profile();
  file.append(name);
  return file;
}

function promiseAsyncDatabase(name, openOptions = 0) {
  return new Promise((resolve, reject) => {
    let file = getProfileFile(name);
    const connOptions = Ci.mozIStorageService.CONNECTION_DEFAULT;
    Services.storage.openAsyncDatabase(
      file,
      openOptions,
      connOptions,
      (status, connection) => {
        if (!Components.isSuccessCode(status)) {
          reject(new Error(`Failed to open database: ${status}`));
        } else {
          connection.QueryInterface(Ci.mozIStorageAsyncConnection);
          resolve(connection);
        }
      }
    );
  });
}

function promiseClone(db, readOnly = false) {
  return new Promise((resolve, reject) => {
    db.asyncClone(readOnly, (status, clone) => {
      if (!Components.isSuccessCode(status)) {
        reject(new Error(`Failed to clone connection: ${status}`));
      } else {
        clone.QueryInterface(Ci.mozIStorageAsyncConnection);
        resolve(clone);
      }
    });
  });
}

function promiseClose(db) {
  return new Promise((resolve, reject) => {
    db.asyncClose(status => {
      if (!Components.isSuccessCode(status)) {
        reject(new Error(`Failed to close connection: ${status}`));
      } else {
        resolve();
      }
    });
  });
}

function promiseExecuteStatement(statement) {
  return new Promise((resolve, reject) => {
    let rows = [];
    statement.executeAsync({
      handleResult(resultSet) {
        let row = null;
        do {
          row = resultSet.getNextRow();
          if (row) {
            rows.push(row);
          }
        } while (row);
      },
      handleError(error) {
        reject(new Error(`Failed to execute statement: ${error.message}`));
      },
      handleCompletion(reason) {
        if (reason == Ci.mozIStorageStatementCallback.REASON_FINISHED) {
          resolve(rows);
        } else {
          reject(new Error("Statement failed to execute or was cancelled"));
        }
      },
    });
  });
}

add_task(async function test_retry_on_busy() {
  info("Open first writer in WAL mode and set up schema");
  let db1 = await promiseAsyncDatabase("retry-on-busy.sqlite");

  let walStmt = db1.createAsyncStatement(`PRAGMA journal_mode = WAL`);
  await promiseExecuteStatement(walStmt);
  let createAStmt = db1.createAsyncStatement(`CREATE TABLE a(
    b INTEGER PRIMARY KEY
  )`);
  await promiseExecuteStatement(createAStmt);
  let createPrevAStmt = db1.createAsyncStatement(`CREATE TEMP TABLE prevA(
    curB INTEGER PRIMARY KEY,
    prevB INTEGER NOT NULL
  )`);
  await promiseExecuteStatement(createPrevAStmt);
  let createATriggerStmt = db1.createAsyncStatement(`
    CREATE TEMP TRIGGER a_afterinsert_trigger
    AFTER UPDATE ON a FOR EACH ROW
    BEGIN
      REPLACE INTO prevA(curB, prevB) VALUES(NEW.b, OLD.b);
    END`);
  await promiseExecuteStatement(createATriggerStmt);

  info("Open second writer");
  let db2 = await promiseClone(db1);

  info("Attach second writer to new database");
  let attachStmt = db2.createAsyncStatement(`ATTACH :path AS newDB`);
  attachStmt.bindByName(
    "path",
    getProfileFile("retry-on-busy-attach.sqlite").path
  );
  await promiseExecuteStatement(attachStmt);

  info("Create triggers on second writer");
  let createCStmt = db2.createAsyncStatement(`CREATE TABLE newDB.c(
    d INTEGER PRIMARY KEY
  )`);
  await promiseExecuteStatement(createCStmt);
  let createCTriggerStmt = db2.createAsyncStatement(`
    CREATE TEMP TRIGGER c_afterdelete_trigger
    AFTER DELETE ON c FOR EACH ROW
    BEGIN
      INSERT INTO a(b) VALUES(OLD.d);
    END`);
  await promiseExecuteStatement(createCTriggerStmt);

  info("Begin transaction on second writer");
  let begin2Stmt = db2.createAsyncStatement("BEGIN IMMEDIATE");
  await promiseExecuteStatement(begin2Stmt);

  info(
    "Begin transaction on first writer; should busy-wait until second writer is done"
  );
  let begin1Stmt = db1.createAsyncStatement("BEGIN IMMEDIATE");
  let promise1Began = promiseExecuteStatement(begin1Stmt);
  let update1Stmt = db1.createAsyncStatement(`UPDATE a SET b = 3 WHERE b = 1`);
  let promise1Updated = promiseExecuteStatement(update1Stmt);

  info("Wait 5 seconds");
  await new Promise(resolve => setTimeout(resolve, 5000));

  info("Commit transaction on second writer");
  let insertIntoA2Stmt = db2.createAsyncStatement(`INSERT INTO a(b) VALUES(1)`);
  await promiseExecuteStatement(insertIntoA2Stmt);
  let insertIntoC2Stmt = db2.createAsyncStatement(`INSERT INTO c(d) VALUES(2)`);
  await promiseExecuteStatement(insertIntoC2Stmt);
  let deleteFromC2Stmt = db2.createAsyncStatement(`DELETE FROM c`);
  await promiseExecuteStatement(deleteFromC2Stmt);
  let commit2Stmt = db2.createAsyncStatement("COMMIT");
  await promiseExecuteStatement(commit2Stmt);

  info("Await and commit transaction on first writer");
  await promise1Began;
  await promise1Updated;

  let commit1Stmt = db1.createAsyncStatement("COMMIT");
  await promiseExecuteStatement(commit1Stmt);

  info("Verify our writes succeeded");
  let select1Stmt = db2.createAsyncStatement("SELECT b FROM a");
  let rows = await promiseExecuteStatement(select1Stmt);
  deepEqual(
    rows.map(row => row.getResultByName("b")),
    [2, 3]
  );

  info("Clean up");
  for (let stmt of [
    walStmt,
    createAStmt,
    createPrevAStmt,
    createATriggerStmt,
    attachStmt,
    createCStmt,
    createCTriggerStmt,
    begin2Stmt,
    begin1Stmt,
    insertIntoA2Stmt,
    insertIntoC2Stmt,
    deleteFromC2Stmt,

    commit2Stmt,
    update1Stmt,
    commit1Stmt,
    select1Stmt,
  ]) {
    stmt.finalize();
  }
  await promiseClose(db1);
  await promiseClose(db2);
});

async function execute(db, sql) {
  let stmt = db.createAsyncStatement(sql);
  try {
    return await promiseExecuteStatement(stmt);
  } finally {
    stmt.finalize();
  }
}

function promiseExecuteStatementOutcome(statement) {
  let error = null;
  let { promise, resolve } = Promise.withResolvers();
  statement.executeAsync({
    handleResult() {},
    handleError(e) {
      error = e;
    },
    handleCompletion(reason) {
      resolve({ reason, error });
    },
  });
  return promise;
}

async function getBusyTimeout(db) {
  let rows = await execute(db, "PRAGMA busy_timeout");
  return rows[0].getResultByIndex(0);
}

add_task(async function test_async_connections_default_busy_timeout() {
  let db = await promiseAsyncDatabase("default-busy-timeout.sqlite");
  Assert.equal(await getBusyTimeout(db), 100, "Async connection has default");

  let readOnlyClone = await promiseClone(db, true);
  Assert.equal(
    await getBusyTimeout(readOnlyClone),
    100,
    "Read-only async clone has default"
  );

  await execute(db, "PRAGMA busy_timeout = 0");
  Assert.equal(await getBusyTimeout(db), 0, "Consumers can disable it");
  let clone = await promiseClone(db);
  Assert.equal(
    await getBusyTimeout(clone),
    100,
    "A disabled timeout is not copied to async clones"
  );

  await execute(db, "PRAGMA busy_timeout = 50");
  let clone2 = await promiseClone(db);
  Assert.equal(
    await getBusyTimeout(clone2),
    100,
    "A set timeout is not copied to async clones"
  );

  let syncDb = Services.storage.openUnsharedDatabase(
    getProfileFile("default-busy-timeout.sqlite")
  );
  let stmt = syncDb.createStatement("PRAGMA busy_timeout");
  Assert.ok(stmt.executeStep());
  Assert.equal(stmt.getInt32(0), 0, "Sync connections have no default");
  stmt.finalize();
  syncDb.close();

  for (let conn of [clone2, clone, readOnlyClone, db]) {
    await promiseClose(conn);
  }
});

add_task(async function test_interrupt_busy_wait() {
  let db1 = await promiseAsyncDatabase("interrupt-busy.sqlite");
  await execute(db1, "PRAGMA journal_mode = WAL");
  await execute(db1, "CREATE TABLE t(id INTEGER PRIMARY KEY)");
  let db2 = await promiseClone(db1);
  await execute(db2, "BEGIN IMMEDIATE");

  let insert = db1.createAsyncStatement("INSERT INTO t DEFAULT VALUES");
  let promise = promiseExecuteStatementOutcome(insert);
  // An interrupt arriving before the statement starts executing is discarded,
  // so keep interrupting until it completes.
  let interval = setInterval(() => db1.interrupt(), 100);
  let { reason } = await promise;
  clearInterval(interval);
  Assert.equal(
    reason,
    Ci.mozIStorageStatementCallback.REASON_CANCELED,
    "Interrupt stops a statement waiting for a lock"
  );
  insert.finalize();

  await execute(db2, "ROLLBACK");
  await promiseClose(db1);
  await promiseClose(db2);
});

add_task(async function test_stale_snapshot_is_not_retried() {
  let db1 = await promiseAsyncDatabase("stale-snapshot.sqlite");
  await execute(db1, "PRAGMA journal_mode = WAL");
  await execute(db1, "CREATE TABLE t(id INTEGER PRIMARY KEY)");
  let db2 = await promiseClone(db1);

  info("Start a read transaction, then write from another connection");
  await execute(db1, "BEGIN DEFERRED");
  await execute(db1, "SELECT * FROM t");
  await execute(db2, "INSERT INTO t DEFAULT VALUES");

  info("Writing on the stale snapshot must fail instead of retrying");
  let insert = db1.createAsyncStatement("INSERT INTO t DEFAULT VALUES");
  let { reason, error } = await promiseExecuteStatementOutcome(insert);
  Assert.equal(reason, Ci.mozIStorageStatementCallback.REASON_ERROR);
  Assert.equal(error.result, Ci.mozIStorageError.BUSY);
  insert.finalize();

  await execute(db1, "ROLLBACK");
  let rows = await execute(db1, "SELECT count(*) FROM t");
  Assert.equal(rows[0].getResultByIndex(0), 1, "Only the other write landed");

  await promiseClose(db1);
  await promiseClose(db2);
});
