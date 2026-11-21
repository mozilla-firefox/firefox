/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const { TestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/TestUtils.sys.mjs"
);

const kInterfaceName = "wifi";
const kHttpsCheckPath = "/httpsCheck";
const kPrefsHttpsCheckEnabled = "captivedetect.httpsCheck.enabled";
const kPrefsHttpsCheckURL = "captivedetect.httpsCheck.url";

let canonicalStatus;
let httpsCheckStatus;
let httpsCheckRequests;
let pendingHttpsCheck;
let onCanonicalRequest;

function canonicalHandler(metadata, response) {
  onCanonicalRequest?.();
  response.setStatusLine(metadata.httpVersion, canonicalStatus, "");
  if (canonicalStatus == 302) {
    response.setHeader("Location", "http://portal.example/login", false);
  }
  response.write("false");
}

function httpsCheckHandler(metadata, response) {
  httpsCheckRequests++;
  if (httpsCheckStatus == null) {
    response.processAsync();
    pendingHttpsCheck = response;
    Services.obs.notifyObservers(null, "test-https-check-pending");
    return;
  }
  response.setStatusLine(metadata.httpVersion, httpsCheckStatus, "");
}

add_setup(async function () {
  gServer = new HttpServer();
  gServer.registerPathHandler(kCanonicalSitePath, canonicalHandler);
  gServer.registerPathHandler(kHttpsCheckPath, httpsCheckHandler);
  gServer.start(-1);
  gServerURL = "http://localhost:" + gServer.identity.primaryPort;
  setupPrefs();
  Services.prefs.setCharPref(kPrefsHttpsCheckURL, gServerURL + kHttpsCheckPath);

  registerCleanupFunction(async () => {
    Services.prefs.clearUserPref(kPrefsHttpsCheckEnabled);
    Services.prefs.clearUserPref(kPrefsHttpsCheckURL);
    await new Promise(resolve => gServer.stop(resolve));
  });
});

let lastUnlockedPortal;

// Resolves with "login" if a captive portal login is requested, or with the
// success value passed to the |complete| callback otherwise.
function runCheck() {
  lastUnlockedPortal = undefined;
  httpsCheckRequests = 0;
  return new Promise(resolve => {
    let onLogin = () => {
      Services.obs.removeObserver(onLogin, "captive-portal-login");
      gCaptivePortalDetector.abort(kInterfaceName);
      resolve("login");
    };
    Services.obs.addObserver(onLogin, "captive-portal-login");

    gCaptivePortalDetector.checkCaptivePortal(kInterfaceName, {
      QueryInterface: ChromeUtils.generateQI(["nsICaptivePortalCallback"]),
      prepare() {
        gCaptivePortalDetector.finishPreparation(kInterfaceName);
      },
      complete(success, unlockedPortal) {
        Services.obs.removeObserver(onLogin, "captive-portal-login");
        lastUnlockedPortal = unlockedPortal;
        resolve(success);
      },
    });
  });
}

add_task(async function test_disabled_does_not_check_https() {
  Services.prefs.setBoolPref(kPrefsHttpsCheckEnabled, false);
  canonicalStatus = 200;
  httpsCheckStatus = 204;

  Assert.equal(await runCheck(), "login");
  Assert.equal(httpsCheckRequests, 0);
});

add_task(async function test_https_success_unlocks() {
  Services.prefs.setBoolPref(kPrefsHttpsCheckEnabled, true);
  for (let status of [200, 302, 511]) {
    canonicalStatus = status;
    for (let httpsStatus of [200, 204]) {
      httpsCheckStatus = httpsStatus;
      Assert.strictEqual(
        await runCheck(),
        true,
        `canonical ${status}, https ${httpsStatus}`
      );
      Assert.strictEqual(lastUnlockedPortal, true);
      Assert.equal(httpsCheckRequests, 1);
    }
  }
});

add_task(async function test_https_failure_starts_login() {
  Services.prefs.setBoolPref(kPrefsHttpsCheckEnabled, true);
  canonicalStatus = 200;
  for (let httpsStatus of [302, 404, 511]) {
    httpsCheckStatus = httpsStatus;
    Assert.equal(await runCheck(), "login", `https ${httpsStatus}`);
    Assert.equal(httpsCheckRequests, 1);
  }
});

add_task(async function test_https_network_error_starts_login() {
  Services.prefs.setBoolPref(kPrefsHttpsCheckEnabled, true);
  Services.prefs.setCharPref(kPrefsHttpsCheckURL, "http://localhost:1/");
  canonicalStatus = 200;

  Assert.equal(await runCheck(), "login");

  Services.prefs.setCharPref(kPrefsHttpsCheckURL, gServerURL + kHttpsCheckPath);
});

add_task(async function test_invalid_https_url_starts_login() {
  Services.prefs.setBoolPref(kPrefsHttpsCheckEnabled, true);
  Services.prefs.setCharPref(kPrefsHttpsCheckURL, "not a url");
  canonicalStatus = 200;

  Assert.equal(await runCheck(), "login");

  canonicalStatus = 302;
  Assert.equal(await runCheck(), "login");

  Services.prefs.setCharPref(kPrefsHttpsCheckURL, gServerURL + kHttpsCheckPath);
});

// The HTTPS check fails until the user logs in, but the portal keeps
// intercepting plain HTTP afterwards.
add_task(async function test_https_success_after_login() {
  Services.prefs.setBoolPref(kPrefsHttpsCheckEnabled, true);
  canonicalStatus = 200;
  httpsCheckStatus = 302;
  httpsCheckRequests = 0;

  let loginStarted = TestUtils.topicObserved("captive-portal-login");
  let loginSucceeded = false;
  let onLoginSuccess = () => (loginSucceeded = true);
  Services.obs.addObserver(onLoginSuccess, "captive-portal-login-success");
  let completed = new Promise(resolve => {
    gCaptivePortalDetector.checkCaptivePortal(kInterfaceName, {
      QueryInterface: ChromeUtils.generateQI(["nsICaptivePortalCallback"]),
      prepare() {
        gCaptivePortalDetector.finishPreparation(kInterfaceName);
      },
      complete(success, unlockedPortal) {
        resolve({ success, unlockedPortal });
      },
    });
  });
  await loginStarted;
  Assert.equal(httpsCheckRequests, 1);

  httpsCheckStatus = 204;
  let polls = 0;
  let stillLocked = new Promise(resolve => {
    onCanonicalRequest = () => {
      if (++polls >= 5) {
        resolve("still locked after login");
      }
    };
  });

  let result = await Promise.race([completed, stillLocked]);
  onCanonicalRequest = null;
  gCaptivePortalDetector.abort(kInterfaceName);
  Services.obs.removeObserver(onLoginSuccess, "captive-portal-login-success");
  Assert.deepEqual(result, { success: true, unlockedPortal: true });
  Assert.ok(loginSucceeded);
});

// captive-portal-check-complete must reflect the HTTPS check result, and only
// fire once a successful result has been reported through |complete|.
add_task(async function test_check_complete_reflects_https_result() {
  Services.prefs.setBoolPref(kPrefsHttpsCheckEnabled, true);
  canonicalStatus = 200;
  let events = [];
  let onCheckComplete = (subject, topic, data) =>
    events.push(`check-complete ${data}`);
  Services.obs.addObserver(onCheckComplete, "captive-portal-check-complete");

  httpsCheckStatus = 204;
  Assert.strictEqual(await runCheck(), true);
  Assert.deepEqual(events, ["check-complete false"]);

  events = [];
  httpsCheckStatus = 302;
  let loginStarted = TestUtils.topicObserved("captive-portal-login");
  let completed = new Promise(resolve => {
    gCaptivePortalDetector.checkCaptivePortal(kInterfaceName, {
      QueryInterface: ChromeUtils.generateQI(["nsICaptivePortalCallback"]),
      prepare() {
        gCaptivePortalDetector.finishPreparation(kInterfaceName);
      },
      complete() {
        events.push("complete");
        resolve();
      },
    });
  });
  await loginStarted;
  Assert.deepEqual(events, ["check-complete true"]);

  events = [];
  httpsCheckStatus = 204;
  await completed;
  Assert.deepEqual(events, ["complete", "check-complete false"]);

  Services.obs.removeObserver(onCheckComplete, "captive-portal-check-complete");
});

add_task(async function test_abort_during_https_check() {
  Services.prefs.setBoolPref(kPrefsHttpsCheckEnabled, true);
  canonicalStatus = 200;
  httpsCheckStatus = null;

  let onLogin = () => Assert.ok(false, "should not start login");
  Services.obs.addObserver(onLogin, "captive-portal-login");
  let pending = TestUtils.topicObserved("test-https-check-pending");
  gCaptivePortalDetector.checkCaptivePortal(kInterfaceName, {
    QueryInterface: ChromeUtils.generateQI(["nsICaptivePortalCallback"]),
    prepare() {
      gCaptivePortalDetector.finishPreparation(kInterfaceName);
    },
    complete() {
      Assert.ok(false, "should not complete after abort");
    },
  });
  await pending;

  gCaptivePortalDetector.abort(kInterfaceName);
  pendingHttpsCheck.setStatusLine("1.1", 204, "");
  pendingHttpsCheck.finish();

  // A new check on the same interface must be accepted after the abort.
  Services.obs.removeObserver(onLogin, "captive-portal-login");
  httpsCheckStatus = 204;
  Assert.strictEqual(await runCheck(), true);
});
