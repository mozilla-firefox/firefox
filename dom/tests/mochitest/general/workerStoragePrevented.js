// Unfortunately, workers can't share the code from storagePermissionsUtils.
// These are basic mechanisms for communicating to the test runner.

function ok(condition, text) {
  if (!condition) {
    self.postMessage("FAILURE: " + text);
  } else {
    self.postMessage(text);
  }
}

function finishTest() {
  self.postMessage("done");
  self.close();
}

// Workers don't have access to localstorage or sessionstorage
ok(typeof self.localStorage == "undefined", "localStorage should be undefined");
ok(
  typeof self.sessionStorage == "undefined",
  "sessionStorage should be undefined"
);

// Make sure that we can access indexedDB handle
try {
  indexedDB;
  ok(true, "WORKER getting indexedDB didn't throw");
} catch (e) {
  ok(false, "WORKER getting indexedDB threw");
}

// Make sure that we cannot access indexedDB methods
idbOpenTest();

// Make sure that we can't access indexedDB deleteDatabase
function idbDeleteTest() {
  try {
    indexedDB.deleteDatabase("door");
    ok(false, "WORKER deleting indexedDB database succeeded");
  } catch (e) {
    ok(true, "WORKER deleting indexedDB database failed");
    ok(
      e.name == "SecurityError",
      "WORKER deleting indexedDB database threw a security error"
    );
  } finally {
    cacheTest();
  }
}

// Make sure that we can't access indexedDB databases
function idbDatabasesTest() {
  indexedDB
    .databases()
    .then(() => {
      ok(false, "WORKER querying indexedDB databases succeeded");
    })
    .catch(e => {
      ok(true, "WORKER querying indexedDB databases failed");
      ok(
        e.name == "SecurityError",
        "WORKER querying indexedDB databases threw a security error"
      );
    })
    .finally(idbDeleteTest);
}

// Make sure that we can't access indexedDB open
function idbOpenTest() {
  try {
    indexedDB.open("door");
    ok(false, "WORKER opening indexedDB database succeeded");
  } catch (e) {
    ok(true, "WORKER opening indexedDB database failed");
    ok(
      e.name == "SecurityError",
      "WORKER opening indexedDB database threw a security error"
    );
  } finally {
    idbDatabasesTest();
  }
}

// Make sure that we can't access caches
function cacheTest() {
  try {
    var promise = caches.keys();
    ok(true, "WORKER getting caches didn't throw");

    promise.then(
      function () {
        ok(false, "WORKER The promise should have rejected");
        workerTest();
      },
      function () {
        ok(true, "WORKER The promise was rejected");
        workerTest();
      }
    );
  } catch (e) {
    ok(!isSecureContext, "WORKER getting caches should not have thrown");
    workerTest();
  }
}

// Try to spawn an inner worker, and make sure that it also can't access storage
function workerTest() {
  if (location.hash != "#outer") {
    // Don't recurse infinitely, if we are the inner worker, don't spawn another
    finishTest();
    return;
  }
  // Create the inner worker, and listen for test messages from it
  var worker = new Worker("workerStoragePrevented.js#inner");
  worker.addEventListener("message", function (e) {
    const isFail = e.data.match(/^FAILURE/);
    ok(!isFail, e.data + " (WORKER = workerStoragePrevented.js#inner)");

    if (e.data == "done" || isFail) {
      finishTest();
    }
  });
  worker.addEventListener("error", function (e) {
    ok(false, e.data + " (WORKER = workerStoragePrevented.js#inner)");

    finishTest();
  });
}
