function testScript(script) {
  var mode = location.href.includes("http2") ? "?mode=http2&" : "?";
  function makeWrapperUrl(wrapper) {
    return wrapper + mode + "script=" + script;
  }
  let workerWrapperUrl = makeWrapperUrl("worker_wrapper.js");

  // The framework runs the entire test in many different configurations.
  // On slow platforms and builds this can make the tests likely to
  // timeout while they are still running.  Lengthen the timeout to
  // accomodate this.
  SimpleTest.requestLongerTimeout(4);

  // reroute.html should have set this variable if a service worker is present!
  if (!("isSWPresent" in window)) {
    window.isSWPresent = false;
  }

  function setupPrefs() {
    return new Promise(function (resolve, reject) {
      SpecialPowers.pushPrefEnv(
        {
          set: [
            ["dom.serviceWorkers.enabled", true],
            ["dom.serviceWorkers.testing.enabled", true],
            ["dom.serviceWorkers.idle_timeout", 60000],
            ["dom.serviceWorkers.exemptFromPerDomainMax", true],
          ],
        },
        resolve
      );
    });
  }

  function workerTest() {
    return new Promise(function (resolve, reject) {
      var worker = new Worker(workerWrapperUrl);
      worker.onmessage = function (event) {
        if (event.data.context != "Worker") {
          return;
        }
        if (event.data.type == "finish") {
          resolve();
        } else if (event.data.type == "status") {
          ok(event.data.status, event.data.context + ": " + event.data.msg);
        }
      };
      worker.onerror = function (event) {
        reject("Worker error: " + event.message);
      };

      worker.postMessage({ script });
    });
  }

  function nestedWorkerTest() {
    return new Promise(function (resolve, reject) {
      var worker = new Worker(makeWrapperUrl("nested_worker_wrapper.js"));
      worker.onmessage = function (event) {
        if (event.data.context != "NestedWorker") {
          return;
        }
        if (event.data.type == "finish") {
          resolve();
        } else if (event.data.type == "status") {
          ok(event.data.status, event.data.context + ": " + event.data.msg);
        }
      };
      worker.onerror = function (event) {
        reject("Nested Worker error: " + event.message);
      };

      worker.postMessage({ script });
    });
  }

  function serviceWorkerTest() {
    return new Promise(function (resolve, reject) {
      function setupSW(registration) {
        var worker =
          registration.installing ||
          registration.waiting ||
          registration.active;
        var iframe;

        window.addEventListener("message", function onMessage(event) {
          if (event.data.context != "ServiceWorker") {
            return;
          }
          if (event.data.type == "finish") {
            window.removeEventListener("message", onMessage);
            iframe.remove();
            registration.unregister().then(resolve).catch(reject);
          } else if (event.data.type == "status") {
            ok(event.data.status, event.data.context + ": " + event.data.msg);
          }
        });

        worker.onerror = reject;

        iframe = document.createElement("iframe");
        iframe.src = "message_receiver.html";
        iframe.onload = function () {
          worker.postMessage({ script });
        };
        document.body.appendChild(iframe);
      }

      navigator.serviceWorker
        .register(workerWrapperUrl, { scope: "." })
        .then(setupSW);
    });
  }

  function windowTest() {
    return new Promise(function (resolve, reject) {
      var scriptEl = document.createElement("script");
      scriptEl.setAttribute("src", script);
      scriptEl.onload = function () {
        runTest().then(resolve, reject);
      };
      document.body.appendChild(scriptEl);
    });
  }

  SimpleTest.waitForExplicitFinish();
  // We have to run the window, worker and service worker tests sequentially
  // since some tests set and compare cookies and running in parallel can lead
  // to conflicting values.
  setupPrefs()
    .then(function () {
      return windowTest();
    })
    .then(function () {
      return workerTest();
    })
    .then(function () {
      return nestedWorkerTest();
    })
    .then(function () {
      return serviceWorkerTest();
    })
    .catch(function (e) {
      ok(false, "Some test failed in " + script);
      info(e);
      info(e.message);
      return Promise.resolve();
    })
    .then(function () {
      try {
        if (parent && parent.finishTest) {
          parent.finishTest();
          return;
        }
      } catch {}
      SimpleTest.finish();
    });
}
