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

// Make sure that we can access indexedDB
function idbTest() {
  try {
    indexedDB;

    const idbcycle = new Promise((resolve, reject) => {
      const begin = indexedDB.open("door");
      begin.onerror = e => {
        reject(e);
      };
      begin.onsuccess = () => {
        indexedDB
          .databases()
          .then(dbs => {
            ok(
              dbs.some(elem => elem.name === "door"),
              "WORKER just created database should be found"
            );
            const end = indexedDB.deleteDatabase("door");
            end.onerror = e => {
              reject(e);
            };
            end.onsuccess = () => {
              resolve();
            };
          })
          .catch(err => {
            reject(err);
          });
      };
    });

    idbcycle.then(
      () => {
        ok(true, "WORKER getting indexedDB didn't throw");
        cacheTest();
      },
      e => {
        ok(false, "WORKER getting indexedDB threw " + e.message);
        cacheTest();
      }
    );
  } catch (e) {
    ok(false, "WORKER getting indexedDB should not throw");
    cacheTest();
  }
}

// Make sure that we can access caches
function cacheTest() {
  try {
    var promise = caches.keys();
    ok(true, "WORKER getting caches didn't throw");

    promise.then(
      function () {
        ok(
          location.protocol == "https:",
          "WORKER The promise was not rejected"
        );
        workerTest();
      },
      function () {
        ok(
          location.protocol !== "https:",
          "WORKER The promise should not have been rejected"
        );
        workerTest();
      }
    );
  } catch (e) {
    ok(!isSecureContext, "WORKER getting caches should not have thrown");
    workerTest();
  }
}

// Try to spawn an inner worker, and make sure that it can also access storage
function workerTest() {
  if (location.hash != "#outer") {
    // Don't recurse infinitely, if we are the inner worker, don't spawn another
    finishTest();
    return;
  }
  // Create the inner worker, and listen for test messages from it
  var worker = new Worker("workerStorageAllowed.js#inner");
  worker.addEventListener("message", function (e) {
    if (e.data == "done") {
      finishTest();
      return;
    }

    ok(
      !e.data.match(/^FAILURE/),
      e.data + " (WORKER = workerStorageAllowed.js#inner)"
    );
  });

  worker.addEventListener("error", function (e) {
    ok(false, e.data + " (WORKER = workerStorageAllowed.js#inner)");

    finishTest();
  });
}

try {
  // Workers don't have access to localstorage or sessionstorage
  ok(
    typeof self.localStorage == "undefined",
    "localStorage should be undefined"
  );
  ok(
    typeof self.sessionStorage == "undefined",
    "sessionStorage should be undefined"
  );

  idbTest();
} catch (e) {
  ok(false, "WORKER Unwelcome exception received");
  finishTest();
}
