/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file tests support for the sqlite-vec extension.

function tensorToBlob(tensor) {
  return new Uint8ClampedArray(new Float32Array(tensor).buffer);
}

add_setup(async function () {
  cleanup();
});

add_task(async function test_synchronous() {
  info("Testing synchronous connection");
  let conn = getOpenedUnsharedDatabase();
  Assert.throws(
    () =>
      conn.executeSimpleSQL(
        `CREATE VIRTUAL TABLE test USING vec0(
          embedding FLOAT[4]
        );`
      ),
    /NS_ERROR_FAILURE/,
    "Should not be able to use vec without loading the extension"
  );

  await loadExtension(conn);

  conn.executeSimpleSQL(
    `
    CREATE VIRTUAL TABLE test USING vec0(
      embedding FLOAT[4]
    )
    `
  );

  let stmt = conn.createStatement(
    `
    INSERT INTO test(rowid, embedding)
    VALUES (1, :vector)
    `
  );
  stmt.bindBlobByName("vector", tensorToBlob([0.3, 0.3, 0.3, 0.3]));
  stmt.executeStep();
  stmt.reset();
  stmt.finalize();

  stmt = conn.createStatement(
    `
    SELECT
      rowid,
      distance
    FROM test
    WHERE embedding MATCH :vector
    ORDER BY distance
    LIMIT 1
    `
  );
  stmt.bindBlobByName("vector", tensorToBlob([0.3, 0.3, 0.3, 0.3]));
  Assert.ok(stmt.executeStep());
  Assert.equal(stmt.getInt32(0), 1);
  Assert.equal(stmt.getDouble(1), 0.0);
  stmt.reset();
  stmt.finalize();

  cleanup();
});

add_task(async function test_asynchronous() {
  info("Testing asynchronous connection");
  let conn = await openAsyncDatabase(getTestDB());

  await Assert.rejects(
    executeSimpleSQLAsync(
      conn,
      `
      CREATE VIRTUAL TABLE test USING vec0(
        embedding float[4]
      )
      `
    ),
    err => err.message.startsWith("no such module"),
    "Should not be able to use vec without loading the extension"
  );

  await loadExtension(conn);

  await executeSimpleSQLAsync(
    conn,
    `
    CREATE VIRTUAL TABLE test USING vec0(
      embedding float[4]
    )
    `
  );

  await asyncClose(conn);
  await IOUtils.remove(getTestDB().path, { ignoreAbsent: true });
});

add_task(async function test_clone() {
  info("Testing cloning synchronous connection loads extensions in clone");
  let conn1 = getOpenedUnsharedDatabase();
  await loadExtension(conn1);

  let conn2 = conn1.clone(false);
  conn2.executeSimpleSQL(
    `
    CREATE VIRTUAL TABLE test USING vec0(
      embedding float[4]
    )
    `
  );

  conn2.close();
  cleanup();
});

add_task(async function test_asyncClone() {
  info("Testing asynchronously cloning connection loads extensions in clone");
  let conn1 = getOpenedUnsharedDatabase();
  await loadExtension(conn1);

  let conn2 = await asyncClone(conn1, false);
  await executeSimpleSQLAsync(
    conn2,
    `
    CREATE VIRTUAL TABLE test USING vec0(
      embedding float[4]
    )
    `
  );

  await asyncClose(conn2);
  await asyncClose(conn1);
  await IOUtils.remove(getTestDB().path, { ignoreAbsent: true });
});

async function loadExtension(conn, ext = "vec") {
  await new Promise((resolve, reject) => {
    conn.loadExtension(ext, status => {
      if (Components.isSuccessCode(status)) {
        resolve();
      } else {
        reject(status);
      }
    });
  });
}
