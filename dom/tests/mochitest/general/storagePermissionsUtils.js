const BEHAVIOR_ACCEPT = 0;
const BEHAVIOR_REJECT_FOREIGN = 1;
const BEHAVIOR_REJECT = 2;
const BEHAVIOR_LIMIT_FOREIGN = 3;

const kPrefName = "network.cookie.cookieBehavior";

// Check if we are in frame, and declare ok and finishTest appropriately
const inFrame = ("" + location).match(/frame/);
if (inFrame) {
  ok = function (a, message) {
    if (!a) {
      parent.postMessage("FAILURE: " + message, "http://mochi.test:8888");
    } else {
      parent.postMessage(message, "http://mochi.test:8888");
    }
  };

  is = function (a, b, message) {
    ok(a === b, `${message} - got ${a}, expected ${b}`);
  };

  finishTest = function () {
    parent.postMessage("done", "http://mochi.test:8888");
  };
} else {
  finishTest = function () {
    SimpleTest.finish();
  };
}

function setCookieBehavior(behavior) {
  return SpecialPowers.pushPrefEnv({ set: [[kPrefName, behavior]] });
}

function runIFrame(url) {
  return new Promise((resolve, reject) => {
    function onMessage(e) {
      if (e.data == "done") {
        resolve();
        window.removeEventListener("message", onMessage);
        return;
      }

      const isFail = e.data.match(/^FAILURE/);
      ok(!isFail, e.data + " (IFRAME = " + url + ")");
      if (isFail) {
        reject(e);
      }
    }
    window.addEventListener("message", onMessage);

    document.querySelector("iframe").src = url;
  });
}

function runWorker(url) {
  return new Promise((resolve, reject) => {
    var worker = new Worker(url);
    worker.addEventListener("message", function (e) {
      if (e.data == "done") {
        resolve();
        return;
      }

      ok(!e.data.match(/^FAILURE/), e.data + " (WORKER = " + url + ")");
    });

    worker.addEventListener("error", function (e) {
      ok(false, e.data + " (WORKER = " + url + ")");
      reject(e);
    });
  });
}

function chromePower(allowed, blockSessionStorage) {
  // localStorage is affected by storage policy.
  try {
    SpecialPowers.wrap(window).localStorage.getItem("X");
    ok(allowed, "getting localStorage from chrome didn't throw");
  } catch (e) {
    ok(!allowed, "getting localStorage from chrome threw");
  }

  // sessionStorage is not. See bug 1183968.
  try {
    SpecialPowers.wrap(window).sessionStorage.getItem("X");
    ok(!blockSessionStorage, "getting sessionStorage from chrome didn't throw");
  } catch (e) {
    ok(blockSessionStorage, "getting sessionStorage from chrome threw");
  }

  // indexedDB is affected by storage policy. Deleting a database rather than
  // opening one leaves nothing behind when it is allowed.
  try {
    SpecialPowers.wrap(window).indexedDB.deleteDatabase("chromePower");
    ok(allowed, "deleting an indexedDB database from chrome didn't throw");
  } catch (e) {
    ok(!allowed, "deleting an indexedDB database from chrome threw");
    is(
      e.name,
      "SecurityError",
      "deleting indexedDB database from chrome emitted a security error"
    );
  }

  // Same with caches, along with the additional https-only requirement.
  try {
    var shouldResolve = allowed && location.protocol == "https:";
    var promise = SpecialPowers.wrap(window).caches.keys();
    ok(true, "getting caches from chrome should never throw");
    return new Promise((resolve, reject) => {
      promise.then(
        function () {
          ok(shouldResolve, "The promise was resolved for chrome");
          resolve();
        },
        function (e) {
          ok(!shouldResolve, "The promise was rejected for chrome: " + e);
          resolve();
        }
      );
    });
  } catch (e) {
    ok(false, "getting caches from chrome threw");
    return undefined;
  }
}

function storageAllowed() {
  try {
    localStorage.getItem("X");
    ok(true, "getting localStorage didn't throw");
  } catch (e) {
    ok(false, "getting localStorage should not throw");
  }

  try {
    sessionStorage.getItem("X");
    ok(true, "getting sessionStorage didn't throw");
  } catch (e) {
    ok(false, "getting sessionStorage should not throw");
  }

  try {
    indexedDB;
    ok(true, "getting indexedDB didn't throw");
  } catch (e) {
    ok(false, "getting indexedDB should not throw");
  }

  const dbName = "db";

  try {
    var promise = caches.keys();
    ok(true, "getting caches didn't throw");

    return new Promise((resolve, reject) => {
      const checkCacheKeys = () => {
        promise.then(
          () => {
            ok(location.protocol == "https:", "The promise was not rejected");
            resolve();
          },
          () => {
            ok(
              location.protocol !== "https:",
              "The promise should not have been rejected"
            );
            resolve();
          }
        );
      };

      const checkDeleteDbAndTheRest = dbs => {
        ok(
          dbs.some(elem => elem.name === dbName),
          "Expected database should be found"
        );

        const end = indexedDB.deleteDatabase(dbName);
        end.onsuccess = checkCacheKeys;
        end.onerror = err => {
          ok(false, "querying indexedDB databases should not throw");
          reject(err);
        };
      };

      const checkDatabasesAndTheRest = () => {
        indexedDB
          .databases()
          .then(checkDeleteDbAndTheRest)
          .catch(err => {
            ok(false, "deleting an indexedDB database should not throw");
            reject(err);
          });
      };

      const begin = indexedDB.open(dbName);
      begin.onsuccess = checkDatabasesAndTheRest;
      begin.onerror = err => {
        ok(false, "opening an indexedDB database should not throw");
        reject(err);
      };
    });
  } catch (e) {
    ok(!isSecureContext, "getting caches should not have thrown");
    return Promise.resolve();
  }
}

function storagePrevented() {
  try {
    localStorage.getItem("X");
    ok(false, "getting localStorage should have thrown");
  } catch (e) {
    ok(true, "getting localStorage threw");
  }

  if (location.hash == "#thirdparty") {
    // No matter what the user's preferences are, we don't block
    // sessionStorage in 3rd-party iframes. We do block them everywhere
    // else however.
    try {
      sessionStorage.getItem("X");
      ok(true, "getting sessionStorage didn't throw");
    } catch (e) {
      ok(false, "getting sessionStorage should not have thrown");
    }
  } else {
    try {
      sessionStorage.getItem("X");
      ok(false, "getting sessionStorage should have thrown");
    } catch (e) {
      ok(true, "getting sessionStorage threw");
    }
  }

  try {
    indexedDB;
    ok(true, "getting indexedDB didn't throw");
  } catch (e) {
    ok(false, "getting indexedDB should not have thrown");
  }

  const dbName = "album";

  try {
    indexedDB.open(dbName);
    ok(false, "opening an indexedDB database didn't throw");
  } catch (e) {
    ok(true, "opening an indexedDB database threw");
    ok(
      e.name == "SecurityError",
      "opening indexedDB database emitted a security error"
    );
  }

  // Note: Security error is expected to be thrown synchronously.
  indexedDB.databases().then(
    () => {
      ok(false, "querying indexedDB databases didn't reject");
    },
    e => {
      ok(true, "querying indexedDB databases rejected");
      ok(
        e.name == "SecurityError",
        "querying indexedDB databases emitted a security error"
      );
    }
  );

  try {
    indexedDB.deleteDatabase(dbName);
    ok(false, "deleting an indexedDB database didn't throw");
  } catch (e) {
    ok(true, "deleting an indexedDB database threw");
    ok(
      e.name == "SecurityError",
      "deleting indexedDB database emitted a security error"
    );
  }

  try {
    var promise = caches.keys();
    ok(true, "getting caches didn't throw");

    return new Promise((resolve, reject) => {
      promise.then(
        function () {
          ok(false, "The promise should have rejected");
          resolve();
        },
        function () {
          ok(true, "The promise was rejected");
          resolve();
        }
      );
    });
  } catch (e) {
    ok(!isSecureContext, "getting caches should not have thrown");

    return Promise.resolve();
  }
}

function task(fn) {
  if (!inFrame) {
    SimpleTest.waitForExplicitFinish();
  }

  var gen = fn();

  function next_step(val, e) {
    var it;
    try {
      if (typeof e !== "undefined") {
        it = gen.throw(e);
      } else {
        it = gen.next(val);
      }
    } catch (e) {
      ok(false, "An error was thrown while stepping: " + e);
      ok(false, "Stack: " + e.stack);
      finishTest();
    }

    if (it.done) {
      finishTest();
      return;
    }
    it.value.then(next_step, e => next_step(null, e));
  }

  if (!gen.then) {
    next_step();
  } else {
    gen.then(finishTest, e => {
      ok(false, "An error was thrown while stepping: " + e);
      ok(false, "Stack: " + e.stack);
      finishTest();
    });
  }
}

// The test will run on a separate window in order to apply the new cookie jar settings.
async function runTestInWindow(test) {
  let w = window.open("window_storagePermissions.html");
  await new Promise(resolve => {
    w.onload = e => {
      resolve();
    };
  });

  return new Promise((resolve, reject) => {
    onmessage = e => {
      if (!e.data.type) {
        w.postMessage("FAILURE: " + e.data, document.referrer);
        ok(false, "No error data type");
        reject(e);
        return;
      }

      if (e.data.type == "finish") {
        w.close();
        resolve();
        return;
      }

      if (e.data.type == "check") {
        const payload = e.data.msg ? e.data.msg : e.data;
        ok(e.data.test, payload);
        const isFail = payload.match(/^FAILURE/) || !e.data.test;
        if (isFail) {
          w.postMessage("FAILURE: " + e.data, document.referrer);
          ok(false, payload);
          w.close();
          reject(e);
        }
        return;
      }

      ok(false, "Unknown message");
    };

    w.postMessage(test.toString(), "*");
  });
}

var thirdparty = "https://example.com/tests/dom/tests/mochitest/general/";
