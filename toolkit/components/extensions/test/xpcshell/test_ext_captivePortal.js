"use strict";

AddonTestUtils.init(this);
AddonTestUtils.overrideCertDB();
AddonTestUtils.createAppInfo(
  "xpcshell@tests.mozilla.org",
  "XPCShell",
  "1",
  "43"
);

/**
 * This duplicates the test from netwerk/test/unit/test_captive_portal_service.js
 * however using an extension to gather the captive portal information.
 */

const PREF_CAPTIVE_ENABLED = "network.captive-portal-service.enabled";
const PREF_CAPTIVE_TESTMODE = "network.captive-portal-service.testMode";
const PREF_CAPTIVE_MINTIME = "network.captive-portal-service.minInterval";
const PREF_CAPTIVE_ENDPOINT = "captivedetect.canonicalURL";
const PREF_CAPTIVE_CANONICAL_CONTENT = "captivedetect.canonicalContent";
const PREF_DNS_NATIVE_IS_LOCALHOST = "network.dns.native-is-localhost";

const SUCCESS_STRING =
  '<meta http-equiv="refresh" content="0;url=https://support.mozilla.org/kb/captive-portal"/>';
let cpResponse = SUCCESS_STRING;

const httpserver = createHttpServer();
httpserver.registerPathHandler("/captive.txt", (request, response) => {
  response.setStatusLine(request.httpVersion, 200, "OK");
  response.setHeader("Content-Type", "text/plain");
  response.write(cpResponse);
});

registerCleanupFunction(() => {
  Services.prefs.clearUserPref(PREF_CAPTIVE_ENABLED);
  Services.prefs.clearUserPref(PREF_CAPTIVE_TESTMODE);
  Services.prefs.clearUserPref(PREF_CAPTIVE_ENDPOINT);
  Services.prefs.clearUserPref(PREF_CAPTIVE_CANONICAL_CONTENT);
  Services.prefs.clearUserPref(PREF_CAPTIVE_MINTIME);
  Services.prefs.clearUserPref(PREF_DNS_NATIVE_IS_LOCALHOST);
});

add_task(async function setup() {
  Services.prefs.setCharPref(PREF_CAPTIVE_CANONICAL_CONTENT, SUCCESS_STRING);
  Services.prefs.setCharPref(
    PREF_CAPTIVE_ENDPOINT,
    `http://localhost:${httpserver.identity.primaryPort}/captive.txt`
  );
  Services.prefs.setBoolPref(PREF_CAPTIVE_TESTMODE, true);
  Services.prefs.setIntPref(PREF_CAPTIVE_MINTIME, 0);
  Services.prefs.setBoolPref(PREF_DNS_NATIVE_IS_LOCALHOST, true);

  Services.prefs.setBoolPref("extensions.eventPages.enabled", true);
  await AddonTestUtils.promiseStartupManager();
});

add_task(async function test_captivePortal_basic() {
  let cps = Cc["@mozilla.org/network/captive-portal-service;1"].getService(
    Ci.nsICaptivePortalService
  );

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      permissions: ["captivePortal"],
      background: { persistent: false },
    },
    isPrivileged: true,
    async background() {
      browser.captivePortal.onConnectivityAvailable.addListener(details => {
        browser.test.log(
          `onConnectivityAvailable received ${JSON.stringify(details)}`
        );
        browser.test.sendMessage("connectivity", details);
      });

      browser.captivePortal.onStateChanged.addListener(details => {
        browser.test.log(`onStateChanged received ${JSON.stringify(details)}`);
        browser.test.sendMessage("state", details);
      });

      browser.captivePortal.canonicalURL.onChange.addListener(details => {
        browser.test.sendMessage("url", details);
      });

      browser.test.onMessage.addListener(async msg => {
        if (msg == "getstate") {
          browser.test.sendMessage(
            "getstate",
            await browser.captivePortal.getState()
          );
        }
      });
    },
  });
  await extension.startup();

  extension.sendMessage("getstate");
  let details = await extension.awaitMessage("getstate");
  equal(details, "unknown", "initial state");

  // The captive portal service is started by nsIOService when the pref becomes true, so we
  // toggle the pref.  We cannot set to false before the extension loads above.
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, false);
  Services.prefs.setBoolPref(PREF_CAPTIVE_ENABLED, true);

  details = await extension.awaitMessage("connectivity");
  equal(details.status, "clear", "initial connectivity");
  extension.sendMessage("getstate");
  details = await extension.awaitMessage("getstate");
  equal(details, "not_captive", "initial state");

  info("REFRESH to other");
  cpResponse = "other";
  cps.recheckCaptivePortal();
  details = await extension.awaitMessage("state");
  equal(details.state, "locked_portal", "state in portal");

  info("REFRESH to success");
  cpResponse = SUCCESS_STRING;
  cps.recheckCaptivePortal();
  details = await extension.awaitMessage("connectivity");
  equal(details.status, "captive", "final connectivity");

  details = await extension.awaitMessage("state");
  equal(details.state, "unlocked_portal", "state after unlocking portal");

  assertPersistentListeners(
    extension,
    "captivePortal",
    "onConnectivityAvailable",
    {
      primed: false,
    }
  );

  assertPersistentListeners(extension, "captivePortal", "onStateChanged", {
    primed: false,
  });

  assertPersistentListeners(extension, "captivePortal", "captiveURL.onChange", {
    primed: false,
  });

  info("Test event page terminate/waken");

  await extension.terminateBackground({ disableResetIdleForTest: true });
  ok(
    !extension.extension.backgroundContext,
    "Background Extension context should have been destroyed"
  );

  assertPersistentListeners(extension, "captivePortal", "onStateChanged", {
    primed: true,
  });
  assertPersistentListeners(
    extension,
    "captivePortal",
    "onConnectivityAvailable",
    {
      primed: true,
    }
  );

  assertPersistentListeners(extension, "captivePortal", "captiveURL.onChange", {
    primed: true,
  });

  info("REFRESH 2nd pass to other");
  cpResponse = "other";
  cps.recheckCaptivePortal();
  details = await extension.awaitMessage("state");
  equal(details.state, "locked_portal", "state in portal");

  info("Test event page terminate/waken with settings");

  await extension.terminateBackground({ disableResetIdleForTest: true });
  ok(
    !extension.extension.backgroundContext,
    "Background Extension context should have been destroyed"
  );

  assertPersistentListeners(extension, "captivePortal", "captiveURL.onChange", {
    primed: true,
  });

  Services.prefs.setStringPref(
    "captivedetect.canonicalURL",
    "http://example.com"
  );
  let url = await extension.awaitMessage("url");
  equal(
    url.value,
    "http://example.com",
    "The canonicalURL setting has the expected value."
  );

  await extension.unload();
});
