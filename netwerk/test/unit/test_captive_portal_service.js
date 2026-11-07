"use strict";

const { HttpServer } = ChromeUtils.importESModule(
  "resource://testing-common/httpd.sys.mjs"
);

let httpserver = null;
ChromeUtils.defineLazyGetter(this, "cpURI", function () {
  return (
    "http://localhost:" + httpserver.identity.primaryPort + "/captive.html"
  );
});

const SUCCESS_STRING =
  '<meta http-equiv="refresh" content="0;url=https://support.mozilla.org/kb/captive-portal"/>';
let cpResponse = SUCCESS_STRING;
function contentHandler(metadata, response) {
  response.setHeader("Content-Type", "text/html");
  response.bodyOutputStream.write(cpResponse, cpResponse.length);
}

const PREF_CAPTIVE_ENABLED = "network.captive-portal-service.enabled";
const PREF_CAPTIVE_TESTMODE = "network.captive-portal-service.testMode";
const PREF_CAPTIVE_ENDPOINT = "captivedetect.canonicalURL";
const PREF_CAPTIVE_CANONICAL_CONTENT = "captivedetect.canonicalContent";
const PREF_CAPTIVE_MINTIME = "network.captive-portal-service.minInterval";
const PREF_CAPTIVE_MAXTIME = "network.captive-portal-service.maxInterval";
const PREF_DNS_NATIVE_IS_LOCALHOST = "network.dns.native-is-localhost";
const PREF_HTTPS_CHECK_ENABLED = "captivedetect.httpsCheck.enabled";
const PREF_HTTPS_CHECK_URL = "captivedetect.httpsCheck.url";

const cps = Cc["@mozilla.org/network/captive-portal-service;1"].getService(
  Ci.nsICaptivePortalService
);

registerCleanupFunction(async () => {
  Services.prefs.clearUserPref(PREF_CAPTIVE_ENABLED);
  Services.prefs.clearUserPref(PREF_CAPTIVE_TESTMODE);
  Services.prefs.clearUserPref(PREF_CAPTIVE_ENDPOINT);
  Services.prefs.clearUserPref(PREF_CAPTIVE_CANONICAL_CONTENT);
  Services.prefs.clearUserPref(PREF_CAPTIVE_MINTIME);
  Services.prefs.clearUserPref(PREF_CAPTIVE_MAXTIME);
  Services.prefs.clearUserPref(PREF_DNS_NATIVE_IS_LOCALHOST);
  Services.prefs.clearUserPref(PREF_HTTPS_CHECK_ENABLED);
  Services.prefs.clearUserPref(PREF_HTTPS_CHECK_URL);

  await new Promise(resolve => {
    httpserver.stop(resolve);
  });
});

function observerPromise(topic) {
  return new Promise(resolve => {
    let observer = {
      QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
      observe(aSubject, aTopic, aData) {
        if (aTopic == topic) {
          Services.obs.removeObserver(observer, topic);
          resolve(aData);
        }
      },
    };
    Services.obs.addObserver(observer, topic);
  });
}

add_task(function setup() {
  httpserver = new HttpServer();
  httpserver.registerPathHandler("/captive.html", contentHandler);
  httpserver.start(-1);

  Services.prefs.setCharPref(PREF_CAPTIVE_CANONICAL_CONTENT, SUCCESS_STRING);
  Services.prefs.setCharPref(PREF_CAPTIVE_ENDPOINT, cpURI);
  Services.prefs.setIntPref(PREF_CAPTIVE_MINTIME, 50);
  Services.prefs.setIntPref(PREF_CAPTIVE_MAXTIME, 100);
  Services.prefs.setBoolPref(PREF_CAPTIVE_TESTMODE, true);
  Services.prefs.setBoolPref(PREF_DNS_NATIVE_IS_LOCALHOST, true);
});

add_task(async function test_simple() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);

  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  let notification = observerPromise("network:captive-portal-connectivity");
  // The service is started by nsIOService when the pref becomes true.
  // We might want to add a method to do this in the future.
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  let observerPayload = await notification;
  equal(observerPayload, "clear");
  equal(cps.state, Ci.nsICaptivePortalService.NOT_CAPTIVE);

  cpResponse = "other";
  notification = observerPromise("captive-portal-login");
  cps.recheckCaptivePortal();
  await notification;
  equal(cps.state, Ci.nsICaptivePortalService.LOCKED_PORTAL);

  cpResponse = SUCCESS_STRING;
  notification = observerPromise("captive-portal-login-success");
  cps.recheckCaptivePortal();
  await notification;
  equal(cps.state, Ci.nsICaptivePortalService.UNLOCKED_PORTAL);
});

// Restores the configuration shared by the tests below, so that a test using
// its own endpoint behaviour doesn't leak into the following ones.
function restoreSharedConfig() {
  Services.prefs.setCharPref(PREF_CAPTIVE_CANONICAL_CONTENT, SUCCESS_STRING);
  httpserver.registerPathHandler("/captive.html", contentHandler);
  cpResponse = SUCCESS_STRING;
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
}

// This test mimics the production configuration, where the canonical endpoint
// returns an empty 204 response when there is no captive portal and no content
// is expected.
add_task(async function test_204_endpoint() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  Services.prefs.setCharPref(PREF_CAPTIVE_CANONICAL_CONTENT, "");

  let captive = false;
  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    if (captive) {
      // A captive portal returns its login page instead of the empty 204.
      response.setStatusLine(metadata.httpVersion, 200, "OK");
      response.setHeader("Content-Type", "text/html");
      let body = "<html>please log in</html>";
      response.bodyOutputStream.write(body, body.length);
    } else {
      response.setStatusLine(metadata.httpVersion, 204, "No Content");
    }
  });

  let notification = observerPromise("network:captive-portal-connectivity");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);
  equal(await notification, "clear", "An empty 204 means no captive portal");
  equal(cps.state, Ci.nsICaptivePortalService.NOT_CAPTIVE);

  captive = true;
  notification = observerPromise("captive-portal-login");
  cps.recheckCaptivePortal();
  await notification;
  equal(
    cps.state,
    Ci.nsICaptivePortalService.LOCKED_PORTAL,
    "A 200 login page instead of the empty 204 means a captive portal"
  );

  captive = false;
  notification = observerPromise("captive-portal-login-success");
  cps.recheckCaptivePortal();
  await notification;
  equal(cps.state, Ci.nsICaptivePortalService.UNLOCKED_PORTAL);

  restoreSharedConfig();
});

// When no content is expected, an empty body means no captive portal whether
// it arrives as a 200 or as a 204 - some deployments answer a generate_204
// style endpoint with an empty 200.
add_task(async function test_empty_body_accepts_200_and_204() {
  for (let status of [200, 204]) {
    Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
    equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

    Services.prefs.setCharPref(PREF_CAPTIVE_CANONICAL_CONTENT, "");
    httpserver.registerPathHandler("/captive.html", (metadata, response) => {
      response.setStatusLine(metadata.httpVersion, status, "");
    });

    let notification = observerPromise("network:captive-portal-connectivity");
    Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);
    equal(await notification, "clear", `An empty ${status} means no portal`);
    equal(cps.state, Ci.nsICaptivePortalService.NOT_CAPTIVE);
  }

  restoreSharedConfig();
});

// A 204 carries no body, so it cannot be the expected content when one is
// configured. Such a response is treated as interference rather than silently
// accepted.
add_task(async function test_204_with_expected_content() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  Services.prefs.setCharPref(PREF_CAPTIVE_CANONICAL_CONTENT, SUCCESS_STRING);
  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    response.setStatusLine(metadata.httpVersion, 204, "No Content");
  });

  let notification = observerPromise("captive-portal-login");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);
  await notification;
  equal(
    cps.state,
    Ci.nsICaptivePortalService.LOCKED_PORTAL,
    "A 204 is not accepted when a non-empty content is expected"
  );

  restoreSharedConfig();
});

// A canonicalURL pointed at a 200 returning endpoint - by an enterprise policy
// or a user override - must keep working even though the default endpoint now
// answers with a 204, as long as canonicalContent is overridden along with it.
add_task(async function test_200_endpoint_still_works() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  // The expected content is whatever the overridden endpoint returns, exactly
  // as it was before the default endpoint moved to a 204.
  Services.prefs.setCharPref(PREF_CAPTIVE_CANONICAL_CONTENT, SUCCESS_STRING);
  httpserver.registerPathHandler("/captive.html", contentHandler);
  cpResponse = SUCCESS_STRING;

  let notification = observerPromise("network:captive-portal-connectivity");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);
  equal(
    await notification,
    "clear",
    "A 200 matching the expected content means no captive portal"
  );
  equal(cps.state, Ci.nsICaptivePortalService.NOT_CAPTIVE);

  cpResponse = "<html>please log in</html>";
  notification = observerPromise("captive-portal-login");
  cps.recheckCaptivePortal();
  await notification;
  equal(cps.state, Ci.nsICaptivePortalService.LOCKED_PORTAL);

  restoreSharedConfig();
});

// When the canonical URL is intercepted but the HTTPS check gets through, the
// network is reported as an unlocked portal rather than a locked one.
add_task(async function test_https_check_reports_unlocked() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  httpserver.registerPathHandler("/captive.html", contentHandler);
  cpResponse = "<html>please log in</html>";
  httpserver.registerPathHandler("/https-check", (metadata, response) => {
    response.setStatusLine(metadata.httpVersion, 204, "No Content");
  });
  Services.prefs.setBoolPref(PREF_HTTPS_CHECK_ENABLED, true);
  Services.prefs.setCharPref(
    PREF_HTTPS_CHECK_URL,
    `http://localhost:${httpserver.identity.primaryPort}/https-check`
  );

  let onLogin = () => Assert.ok(false, "Should not start a login");
  Services.obs.addObserver(onLogin, "captive-portal-login");
  let connectivityStates = [];
  let mirroredStates = [];
  let onConnectivityChanged = subject => {
    connectivityStates.push(
      subject.QueryInterface(Ci.nsICaptivePortalService).state
    );
  };
  let onStateMirrored = subject => {
    mirroredStates.push(
      subject.QueryInterface(Ci.nsICaptivePortalService).state
    );
  };
  Services.obs.addObserver(
    onConnectivityChanged,
    "network:captive-portal-connectivity-changed"
  );
  Services.obs.addObserver(
    onStateMirrored,
    "ipc:network:captive-portal-set-state"
  );
  let notification = observerPromise("network:captive-portal-connectivity");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  equal(await notification, "captive");
  equal(cps.state, Ci.nsICaptivePortalService.UNLOCKED_PORTAL);

  notification = observerPromise("network:captive-portal-connectivity");
  cps.recheckCaptivePortal();
  equal(await notification, "captive");

  Services.obs.removeObserver(onLogin, "captive-portal-login");
  Services.obs.removeObserver(
    onConnectivityChanged,
    "network:captive-portal-connectivity-changed"
  );
  Services.obs.removeObserver(
    onStateMirrored,
    "ipc:network:captive-portal-set-state"
  );
  Assert.deepEqual(connectivityStates, [
    Ci.nsICaptivePortalService.UNLOCKED_PORTAL,
  ]);
  Assert.deepEqual(mirroredStates, [
    Ci.nsICaptivePortalService.UNLOCKED_PORTAL,
  ]);
  Services.prefs.clearUserPref(PREF_HTTPS_CHECK_ENABLED);
  Services.prefs.clearUserPref(PREF_HTTPS_CHECK_URL);
  restoreSharedConfig();
});

// The HTTPS check fails until the user logs in, but the portal keeps
// intercepting plain HTTP afterwards. The login polling must notice the HTTPS
// check getting through and unlock the portal.
add_task(async function test_https_check_unlocks_after_login() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  let canonicalRequests = 0;
  let onCanonicalRequest = null;
  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    canonicalRequests++;
    onCanonicalRequest?.();
    contentHandler(metadata, response);
  });
  cpResponse = "<html>please log in</html>";
  let loggedIn = false;
  httpserver.registerPathHandler("/https-check", (metadata, response) => {
    if (loggedIn) {
      response.setStatusLine(metadata.httpVersion, 204, "No Content");
    } else {
      response.setStatusLine(metadata.httpVersion, 302, "Found");
      response.setHeader("Location", cpURI);
    }
  });
  Services.prefs.setBoolPref(PREF_HTTPS_CHECK_ENABLED, true);
  Services.prefs.setCharPref(
    PREF_HTTPS_CHECK_URL,
    `http://localhost:${httpserver.identity.primaryPort}/https-check`
  );

  let notification = observerPromise("captive-portal-login");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);
  await notification;
  equal(cps.state, Ci.nsICaptivePortalService.LOCKED_PORTAL);

  loggedIn = true;
  let pollsAtLogin = canonicalRequests;
  let stillLocked = new Promise(resolve => {
    onCanonicalRequest = () => {
      if (canonicalRequests - pollsAtLogin >= 3) {
        resolve("locked");
      }
    };
  });
  let loginSuccess = observerPromise("captive-portal-login-success").then(
    () => "unlocked"
  );

  equal(await Promise.race([loginSuccess, stillLocked]), "unlocked");
  equal(cps.state, Ci.nsICaptivePortalService.UNLOCKED_PORTAL);

  Services.prefs.clearUserPref(PREF_HTTPS_CHECK_ENABLED);
  Services.prefs.clearUserPref(PREF_HTTPS_CHECK_URL);
  restoreSharedConfig();
});

// Stopping the service resets the state to UNKNOWN, which must be mirrored to
// content processes exactly once.
add_task(async function test_stop_mirrors_unknown_state() {
  let mirroredStates = [];
  let onStateMirrored = subject => {
    mirroredStates.push(
      subject.QueryInterface(Ci.nsICaptivePortalService).state
    );
  };

  for (let response of [SUCCESS_STRING, "<html>please log in</html>"]) {
    Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
    equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

    httpserver.registerPathHandler("/captive.html", contentHandler);
    cpResponse = response;
    let notification =
      response == SUCCESS_STRING
        ? observerPromise("network:captive-portal-connectivity")
        : observerPromise("captive-portal-login");
    Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);
    await notification;
    notEqual(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

    mirroredStates = [];
    Services.obs.addObserver(
      onStateMirrored,
      "ipc:network:captive-portal-set-state"
    );
    Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
    Services.obs.removeObserver(
      onStateMirrored,
      "ipc:network:captive-portal-set-state"
    );
    equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);
    Assert.deepEqual(
      mirroredStates,
      [Ci.nsICaptivePortalService.UNKNOWN],
      `Stopping after "${response}"`
    );
  }

  restoreSharedConfig();
});

// This test redirects to another URL which returns the same content.
// It should still be interpreted as a captive portal.
add_task(async function test_redirect_success() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  httpserver.registerPathHandler("/succ.txt", (metadata, response) => {
    response.setHeader("Content-Type", "text/html");
    response.bodyOutputStream.write(cpResponse, cpResponse.length);
  });
  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    response.setStatusLine(metadata.httpVersion, 307, "Moved Temporarily");
    response.setHeader(
      "Location",
      `http://localhost:${httpserver.identity.primaryPort}/succ.txt`
    );
  });

  let notification = observerPromise("captive-portal-login").then(
    () => "login"
  );
  let succNotif = observerPromise("network:captive-portal-connectivity").then(
    () => "connectivity"
  );
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  let winner = await Promise.race([notification, succNotif]);
  equal(winner, "login", "This should have been a login, not a success");
  equal(
    cps.state,
    Ci.nsICaptivePortalService.LOCKED_PORTAL,
    "Should be locked after redirect to same text"
  );
});

// This redirects to another URI with a different content.
// We check that it triggers a captive portal login
add_task(async function test_redirect_bad() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  httpserver.registerPathHandler("/bad.txt", (metadata, response) => {
    response.setHeader("Content-Type", "text/html");
    response.bodyOutputStream.write("bad", "bad".length);
  });

  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    response.setStatusLine(metadata.httpVersion, 307, "Moved Temporarily");
    response.setHeader(
      "Location",
      `http://localhost:${httpserver.identity.primaryPort}/bad.txt`
    );
  });

  let notification = observerPromise("captive-portal-login");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  await notification;
  equal(
    cps.state,
    Ci.nsICaptivePortalService.LOCKED_PORTAL,
    "Should be locked after redirect to bad text"
  );
});

// This redirects to the same URI.
// We check that it triggers a captive portal login
add_task(async function test_redirect_loop() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  // This is actually a redirect loop
  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    response.setStatusLine(metadata.httpVersion, 307, "Moved Temporarily");
    response.setHeader("Location", cpURI);
  });

  let notification = observerPromise("captive-portal-login");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  await notification;
  equal(cps.state, Ci.nsICaptivePortalService.LOCKED_PORTAL);
});

// This redirects to a https URI.
// We check that it triggers a captive portal login
add_task(async function test_redirect_https() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  let h2Port = Services.env.get("MOZHTTP2_PORT");
  Assert.notEqual(h2Port, null);
  Assert.notEqual(h2Port, "");

  // Any kind of redirection should trigger the captive portal login.
  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    response.setStatusLine(metadata.httpVersion, 307, "Moved Temporarily");
    response.setHeader("Location", `https://foo.example.com:${h2Port}/exit`);
  });

  let notification = observerPromise("captive-portal-login");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  await notification;
  equal(
    cps.state,
    Ci.nsICaptivePortalService.LOCKED_PORTAL,
    "Should be locked after redirect to https"
  );
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
});

// This test uses a 511 status code to request a captive portal login
// We check that it triggers a captive portal login
// See RFC 6585 for details
add_task(async function test_511_error() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    response.setStatusLine(
      metadata.httpVersion,
      511,
      "Network Authentication Required"
    );
    cpResponse = '<meta http-equiv="refresh" content="0;url=/login">';
    contentHandler(metadata, response);
  });

  let notification = observerPromise("captive-portal-login");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  await notification;
  equal(cps.state, Ci.nsICaptivePortalService.LOCKED_PORTAL);
});

// Any other 5xx HTTP error, is assumed to be an issue with the
// canonical web server, and should not trigger a captive portal login
add_task(async function test_generic_5xx_error() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  let requests = 0;
  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    if (requests++ === 0) {
      // on first attempt, send 503 error
      response.setStatusLine(
        metadata.httpVersion,
        503,
        "Internal Server Error"
      );
      cpResponse = "<h1>Internal Server Error</h1>";
    } else {
      // on retry, send canonical reply
      cpResponse = SUCCESS_STRING;
    }
    contentHandler(metadata, response);
  });

  let notification = observerPromise("network:captive-portal-connectivity");
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  await notification;
  equal(requests, 2);
  equal(cps.state, Ci.nsICaptivePortalService.NOT_CAPTIVE);
});

add_task(async function test_changed_notification() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  httpserver.registerPathHandler("/captive.html", contentHandler);
  cpResponse = SUCCESS_STRING;

  let changedNotificationCount = 0;
  let observer = {
    QueryInterface: ChromeUtils.generateQI(["nsIObserver"]),
    observe() {
      changedNotificationCount += 1;
    },
  };
  Services.obs.addObserver(
    observer,
    "network:captive-portal-connectivity-changed"
  );

  let notification = observerPromise(
    "network:captive-portal-connectivity-changed"
  );
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);
  await notification;
  equal(changedNotificationCount, 1);
  equal(cps.state, Ci.nsICaptivePortalService.NOT_CAPTIVE);

  notification = observerPromise("network:captive-portal-connectivity");
  cps.recheckCaptivePortal();
  await notification;
  equal(changedNotificationCount, 1);
  equal(cps.state, Ci.nsICaptivePortalService.NOT_CAPTIVE);

  notification = observerPromise("captive-portal-login");
  cpResponse = "you are captive";
  cps.recheckCaptivePortal();
  await notification;
  equal(changedNotificationCount, 1);
  equal(cps.state, Ci.nsICaptivePortalService.LOCKED_PORTAL);

  notification = observerPromise("captive-portal-login-success");
  cpResponse = SUCCESS_STRING;
  cps.recheckCaptivePortal();
  await notification;
  equal(changedNotificationCount, 2);
  equal(cps.state, Ci.nsICaptivePortalService.UNLOCKED_PORTAL);

  notification = observerPromise("captive-portal-login");
  cpResponse = "you are captive";
  cps.recheckCaptivePortal();
  await notification;
  equal(changedNotificationCount, 2);
  equal(cps.state, Ci.nsICaptivePortalService.LOCKED_PORTAL);

  Services.obs.removeObserver(
    observer,
    "network:captive-portal-connectivity-changed"
  );
});

add_task(async function test_disabled_service_does_not_check() {
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  let requests = 0;
  httpserver.registerPathHandler("/captive.html", (metadata, response) => {
    requests++;
    contentHandler(metadata, response);
  });

  cps.recheckCaptivePortal();
  Services.obs.notifyObservers(null, "captive-portal-login-success");

  // Wait for several minInterval periods so a rearmed timer would have fired.
  await new Promise(resolve => do_timeout(500, resolve));

  equal(requests, 0, "No check is performed while the service is disabled");

  Services.obs.notifyObservers(null, "captive-portal-login-abort");
  equal(cps.state, Ci.nsICaptivePortalService.UNKNOWN);

  restoreSharedConfig();
});
