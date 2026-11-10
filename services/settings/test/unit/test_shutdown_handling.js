/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(() => {
  Services.prefs.setStringPref("services.settings.loglevel", "debug");
  registerCleanupFunction(() => {
    Services.prefs.clearUserPref("services.settings.loglevel");
  });
});

add_task(async function test_shutdown_abort_after_start() {
  // Start a forever transaction:
  let counter = 0;
  let transactionStarted;
  let startedPromise = new Promise(r => {
    transactionStarted = r;
  });
  let promise = Database._executeIDB(
    "records",
    store => {
      // Signal we've started.
      transactionStarted();
      function makeRequest() {
        if (++counter > 1000) {
          Assert.ok(
            false,
            "We ran 1000 requests and didn't get aborted, what?"
          );
          return;
        }
        dump("Making request " + counter + "\n");
        const request = store
          .index("cid")
          .openCursor(IDBKeyRange.only("foopydoo/foo"));
        request.onsuccess = () => {
          makeRequest();
        };
      }
      makeRequest();
    },
    { mode: "readonly" }
  );

  // Wait for the transaction to start.
  await startedPromise;

  Database._shutdownHandler(); // should abort the readonly transaction.

  let rejection;
  await promise.catch(e => {
    rejection = e;
  });
  ok(rejection, "Promise should have rejected.");

  // Now clear the shutdown flag and rejection error:
  Database._cancelShutdown();
  rejection = null;
});

add_task(async function test_shutdown_immediate_abort() {
  // Now abort directly from the successful request.
  let promise = Database._executeIDB(
    "records",
    store => {
      let request = store
        .index("cid")
        .openCursor(IDBKeyRange.only("foopydoo/foo"));
      request.onsuccess = () => {
        // Abort immediately.
        Database._shutdownHandler();
        request = store
          .index("cid")
          .openCursor(IDBKeyRange.only("foopydoo/foo"));
        Assert.ok(false, "IndexedDB allowed opening a cursor after aborting?!");
      };
    },
    { mode: "readonly" }
  );

  let rejection;
  // Wait for the abort
  await promise.catch(e => {
    rejection = e;
  });
  ok(rejection, "Directly aborted promise should also have rejected.");
  // Now clear the shutdown flag and rejection error:
  Database._cancelShutdown();
});

add_task(async function test_delete_attachments_stops_at_shutdown() {
  const db = new Database("main/delete-attachments");
  const ids = Array.from({ length: 200 }, (_, i) => `id-${i}`);
  const countAttachments = async () => {
    let count = 0;
    for (const id of ids) {
      if (await db.getAttachment(id)) {
        count++;
      }
    }
    return count;
  };
  const saveAll = () =>
    db.saveAttachments(ids.map(id => [id, { blob: new Blob([id]) }]));

  await saveAll();
  await db.deleteAttachments(ids);
  equal(await countAttachments(), 0, "All attachments should be deleted");

  await saveAll();
  const deletePromise = db.deleteAttachments(ids);
  // This read is queued behind the first chunk of deletions.
  await db.getAttachment(ids[0]);
  Database._shutdownHandler();
  await Assert.rejects(
    deletePromise,
    /shutting down/,
    "Deleting should stop once shutdown has started"
  );

  Database._cancelShutdown();
  const remaining = await countAttachments();
  Assert.greater(
    remaining,
    0,
    "Deletion should have stopped before the last chunk"
  );
  Assert.less(remaining, ids.length, "Some chunks should have been deleted");
  await db.deleteAttachments(ids);
});

add_task(async function test_shutdown_worker() {
  let client = new RemoteSettingsClient("language-dictionaries");
  const before = await client.db.getLastModified();
  Assert.equal(before, null);

  let records = [{}];
  let importPromise = RemoteSettingsWorker._execute(
    "_test_only_import",
    ["main", "language-dictionaries", records, 0],
    { mustComplete: true }
  );
  let stringifyPromise = RemoteSettingsWorker.canonicalStringify(
    [],
    [],
    Date.now()
  );
  // Change the idle time so we shut the worker down even though we can't
  // set gShutdown from outside of the worker management code.
  Services.prefs.setIntPref(
    "services.settings.worker_idle_max_milliseconds",
    1
  );
  RemoteSettingsWorker._abortCancelableRequests();
  await Assert.rejects(
    stringifyPromise,
    /Shutdown/,
    "Should have aborted the stringify request at shutdown."
  );
  await Assert.rejects(
    importPromise,
    /shutting down/,
    "Ensure imports get aborted during shutdown"
  );
  const after = await client.db.getLastModified();
  Assert.equal(after, null);
  await TestUtils.waitForCondition(() => !RemoteSettingsWorker.worker);
  Assert.ok(
    !RemoteSettingsWorker.worker,
    "Worker should have been terminated."
  );
});

// This test must remain the last task of this test file, because
// shutdown phases cannot be reverted.
add_task(async function test_no_new_work_after_shutdown_began() {
  Assert.ok(!RemoteSettingsWorker.worker, "Worker is not running.");
  const db = new Database("main/delete-attachments");
  await db.saveAttachments([["id-0", { blob: new Blob(["id-0"]) }]]);

  Services.startup.advanceShutdownPhase(
    Ci.nsIAppStartup.SHUTDOWN_PHASE_APPSHUTDOWNCONFIRMED
  );
  Assert.ok(Services.startup.shuttingDown, "App shutdown should have started");

  await Assert.rejects(
    RemoteSettingsWorker.importJSONDump("main", "language-dictionaries"),
    /Remote Settings has shut down/,
    "Should refuse new work once app shutdown has started."
  );
  Assert.ok(
    !RemoteSettingsWorker.worker,
    "Worker should not have been instantiated during shutdown."
  );

  await Assert.rejects(
    db.deleteAttachments(["id-0"]),
    /shutting down/,
    "Should refuse to delete attachments once app shutdown has started."
  );
  Assert.ok(
    await db.getAttachment("id-0"),
    "The attachment should not have been deleted"
  );
});
