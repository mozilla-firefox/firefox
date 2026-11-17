/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * This test verifies that cache.put() rejects, and removes the body files it
 * wrote, when the transaction storing the entry can't be started. Another
 * connection holds the database write lock, so the BEGIN IMMEDIATE of the
 * Cache connection fails with SQLITE_BUSY.
 */

async function getCachedUsage() {
  const request = Services.qms.getCachedUsageForPrincipal(
    Services.scriptSecurityManager.getSystemPrincipal()
  );
  return requestFinished(request);
}

add_task(async function testSteps() {
  const url = "http://example.com/index.html";
  const body = "hello world";

  const cache = await caches.open("xpcshell-test");

  const usageBefore = await getCachedUsage();

  info("Holding the database write lock from another connection");

  const dbFile = getCacheDir();
  dbFile.append("caches.sqlite");
  const conn = await Sqlite.openConnection({ path: dbFile.path });
  try {
    await conn.execute("BEGIN IMMEDIATE");
    try {
      await Assert.rejects(
        cache.put(url, new Response(body)),
        () => true,
        "put() is rejected when its transaction can't be started"
      );
    } finally {
      await conn.execute("ROLLBACK");
    }
  } finally {
    await conn.close();
  }

  Assert.equal(getBodyFiles().length, 0, "Body files were removed");
  Assert.equal(await getCachedUsage(), usageBefore, "Usage is unchanged");
  Assert.equal((await cache.keys()).length, 0, "No entry was stored");

  info("Checking that the cache is still usable");

  await cache.put(url, new Response(body));
  const response = await cache.match(url);
  ok(response, "Entry was stored");
  Assert.equal(await response.text(), body, "Body is correct");

  await caches.delete("xpcshell-test");
});
