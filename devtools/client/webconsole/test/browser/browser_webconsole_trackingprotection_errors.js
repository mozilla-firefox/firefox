/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Load a page with tracking elements that get blocked and make sure that a
// 'learn more' link shows up in the webconsole.

"use strict";
requestLongerTimeout(2);

const TEST_PATH = "browser/devtools/client/webconsole/test/browser/";
const TEST_FILE = TEST_PATH + "test-trackingprotection-securityerrors.html";
const TEST_FILE_THIRD_PARTY_ONLY =
  TEST_PATH + "test-trackingprotection-securityerrors-thirdpartyonly.html";
const TEST_URI = "https://example.com/" + TEST_FILE;
const TEST_URI_THIRD_PARTY_ONLY =
  "https://example.com/" + TEST_FILE_THIRD_PARTY_ONLY;
const TRACKER_URL = "https://tracking.example.org/";
const THIRD_PARTY_URL = "https://example.org/";
const BLOCKED_URL = `\u201c${
  TRACKER_URL + TEST_PATH + "cookieSetter.html"
}\u201d`;
const PARTITIONED_URL = `\u201c${
  THIRD_PARTY_URL + TEST_PATH
}cookieSetter.html\u201d`;

const COOKIE_BEHAVIOR_PREF = "network.cookie.cookieBehavior";
const COOKIE_BEHAVIORS = {
  // reject all third-party cookies
  REJECT_FOREIGN: 1,
  // reject all cookies
  REJECT: 2,
  // reject third-party cookies unless the eTLD already has at least one cookie
  LIMIT_FOREIGN: 3,
  // reject trackers
  REJECT_TRACKER: 4,
  // dFPI - partitioned access to third-party cookies
  PARTITION_FOREIGN: 5,
};

const { UrlClassifierTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/UrlClassifierTestUtils.sys.mjs"
);

registerCleanupFunction(async function () {
  UrlClassifierTestUtils.cleanupTestTrackers();

  await new Promise(resolve => {
    Services.clearData.deleteData(Ci.nsIClearDataService.CLEAR_ALL, () =>
      resolve()
    );
  });
});

pushPref("devtools.webconsole.groupWarningMessages", false);

add_task(async function testEnhancedTrackingProtectionMessage() {
  await UrlClassifierTestUtils.addTestTrackers();

  await pushPref("privacy.trackingprotection.enabled", true);
  const hud = await openNewTabAndConsole(TRACKER_URL + TEST_FILE);

  info("Test tracking protection message");
  const message = await waitFor(() =>
    findWarningMessage(
      hud,
      `The resource at \u201chttps://tracking.example.com/\u201d was blocked because ` +
        `Enhanced Tracking Protection is enabled`
    )
  );

  await testLearnMoreClickOpenNewTab(
    message,
    "https://developer.mozilla.org/Firefox/Privacy/Guides/Tracking_Protection" +
      DOCS_GA_PARAMS
  );
});

add_task(async function testForeignCookieBlockedMessage() {
  info("Test foreign cookie blocked message");
  // Bug 1518138: GC heuristics are broken for this test, so that the test
  // ends up running out of memory. Try to work-around the problem by GCing
  // before the test begins.
  Cu.forceShrinkingGC();
  // We change the pref and open a new window to ensure it will be taken into account.
  await pushPref(COOKIE_BEHAVIOR_PREF, COOKIE_BEHAVIORS.REJECT_FOREIGN);
  const { hud, win } = await openNewWindowAndConsole(TEST_URI);
  const message = await waitFor(() =>
    findWarningMessage(
      hud,
      `Request to access cookie or storage on ${BLOCKED_URL} was blocked because we are ` +
        `blocking all third-party storage access requests and Enhanced Tracking Protection is enabled`
    )
  );
  await testLearnMoreClickOpenNewTab(
    message,
    getStorageErrorUrl("CookieBlockedForeign")
  );
  // We explicitely destroy the toolbox in order to ensure waiting for its full destruction
  // and avoid leak / pending requests
  await hud.toolbox.destroy();
  win.close();
});

add_task(async function testLimitForeignCookieBlockedMessage() {
  info("Test unvisited eTLD foreign cookies blocked message");
  // Bug 1518138: GC heuristics are broken for this test, so that the test
  // ends up running out of memory. Try to work-around the problem by GCing
  // before the test begins.
  Cu.forceShrinkingGC();
  // We change the pref and open a new window to ensure it will be taken into account.
  await pushPref(COOKIE_BEHAVIOR_PREF, COOKIE_BEHAVIORS.LIMIT_FOREIGN);
  const { hud, win } = await openNewWindowAndConsole(TEST_URI);

  const message = await waitFor(
    () =>
      findWarningMessage(
        hud,
        `Request to access cookie or storage on ${BLOCKED_URL} was blocked because we are ` +
          `blocking all third-party storage access requests and Enhanced Tracking Protection is enabled`
      ),
    "Wait for 'blocking all third-party storage access' message",
    100
  );
  ok(true, "Third-party storage access blocked message was displayed");

  info("Check that clicking on the Learn More link works as expected");
  await testLearnMoreClickOpenNewTab(
    message,
    getStorageErrorUrl("CookieBlockedForeign")
  );
  // We explicitely destroy the toolbox in order to ensure waiting for its full destruction
  // and avoid leak / pending requests
  await hud.toolbox.destroy();
  win.close();
});

add_task(async function testAllCookieBlockedMessage() {
  info("Test all cookies blocked message");
  // We change the pref and open a new window to ensure it will be taken into account.
  await pushPref(COOKIE_BEHAVIOR_PREF, COOKIE_BEHAVIORS.REJECT);
  const { hud, win } = await openNewWindowAndConsole(TEST_URI);

  const message = await waitFor(() =>
    findWarningMessage(
      hud,
      `Request to access cookie or storage on ${BLOCKED_URL} was blocked because we are ` +
        `blocking all storage access requests`
    )
  );
  await testLearnMoreClickOpenNewTab(
    message,
    getStorageErrorUrl("CookieBlockedAll")
  );
  // We explicitely destroy the toolbox in order to ensure waiting for its full destruction
  // and avoid leak / pending requests
  await hud.toolbox.destroy();
  win.close();
});

add_task(async function testTrackerCookieBlockedMessage() {
  info("Test tracker cookie blocked message");
  // We change the pref and open a new window to ensure it will be taken into account.
  await pushPref(COOKIE_BEHAVIOR_PREF, COOKIE_BEHAVIORS.REJECT_TRACKER);
  const { hud, win } = await openNewWindowAndConsole(TEST_URI);

  const message = await waitFor(() =>
    findWarningMessage(
      hud,
      `Request to access cookie or storage on ${BLOCKED_URL} was blocked because it came ` +
        `from a tracker and Enhanced Tracking Protection is enabled`
    )
  );
  await testLearnMoreClickOpenNewTab(
    message,
    getStorageErrorUrl("CookieBlockedTracker")
  );
  // We explicitely destroy the toolbox in order to ensure waiting for its full destruction
  // and avoid leak / pending requests
  await hud.toolbox.destroy();
  win.close();
});

add_task(async function testForeignCookiePartitionedMessage() {
  info("Test tracker cookie blocked message");
  // We change the pref and open a new window to ensure it will be taken into account.
  await pushPref(COOKIE_BEHAVIOR_PREF, COOKIE_BEHAVIORS.PARTITION_FOREIGN);
  const { hud, win } = await openNewWindowAndConsole(TEST_URI_THIRD_PARTY_ONLY);

  const message = await waitFor(() =>
    findWarningMessage(
      hud,
      `Partitioned cookie or storage access was provided to ${PARTITIONED_URL} because it is ` +
        `loaded in the third-party context and dynamic state partitioning is enabled.`
    )
  );
  await testLearnMoreClickOpenNewTab(
    message,
    getStorageErrorUrl("CookiePartitionedForeign")
  );
  // We explicitely destroy the toolbox in order to ensure waiting for its full destruction
  // and avoid leak / pending requests
  await hud.toolbox.destroy();
  win.close();
});

add_task(async function testCookieBlockedByPermissionMessage() {
  info("Test cookie blocked by permission message");
  // Turn off tracking protection and add a block permission on the URL.
  await pushPref("privacy.trackingprotection.enabled", false);
  const p =
    Services.scriptSecurityManager.createContentPrincipalFromOrigin(
      TRACKER_URL
    );
  Services.perms.addFromPrincipal(
    p,
    "cookie",
    Ci.nsIPermissionManager.DENY_ACTION
  );

  const { hud, win } = await openNewWindowAndConsole(TEST_URI);
  const message = await waitFor(() =>
    findWarningMessage(
      hud,
      `Request to access cookies or ` +
        `storage on ${BLOCKED_URL} was blocked because of custom cookie permission`
    )
  );
  await testLearnMoreClickOpenNewTab(
    message,
    getStorageErrorUrl("CookieBlockedByPermission")
  );
  // We explicitely destroy the toolbox in order to ensure waiting for its full destruction
  // and avoid leak / pending requests
  await hud.toolbox.destroy();
  win.close();

  // Remove the custom permission.
  Services.perms.removeFromPrincipal(p, "cookie");
});

add_task(async function testCookieBlockedForUserContentResourceMessage() {
  info("Test usercontent resource cookie blocking");
  // Bug 1518138: GC heuristics are broken for this test, so that the test
  // ends up running out of memory. Try to work-around the problem by GCing
  // before the test begins.
  Cu.forceShrinkingGC();
  await pushPref("privacy.antitracking.isolateContentScriptResources", true);

  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      content_scripts: [
        {
          matches: [`*://example.com/*/test-blank.html`],
          js: ["content_scripts.js"],
          all_frames: true,
        },
      ],
    },
    files: {
      "content_scripts.js": `
            // An image
            const img = document.createElement("img");
            img.src = "https://example.com/${TEST_PATH}test-image.png";
            document.body.appendChild(img);

            // A script
            const script = document.createElement("script");
            script.src = "https://example.com/${TEST_PATH}empty-with-cookie.js";
            document.body.appendChild(script);

            // An iframe
            const iframe = document.createElement("iframe");
            iframe.src = "https://example.com/${TEST_PATH}test-blank-with-cookie.html";
            document.body.appendChild(iframe);
          `,
    },
  });

  await extension.startup();

  const { hud, win } = await openNewWindowAndConsole(
    "https://example.com/" + TEST_PATH + "test-blank.html"
  );

  await waitFor(
    () =>
      findWarningMessage(
        hud,
        "Request to access cookie or storage on “https://example.com/" +
          `${TEST_PATH}test-image.png” was blocked because we are blocking all ` +
          "storage access requests."
      ) &&
      findWarningMessage(
        hud,
        "Request to access cookie or storage on “https://example.com/" +
          `${TEST_PATH}empty-with-cookie.js” was blocked because we are ` +
          "blocking all storage access requests."
      ) &&
      findWarningMessage(
        hud,
        "Request to access cookie or storage on “https://example.com/" +
          `${TEST_PATH}test-blank-with-cookie.html” was blocked because ` +
          "we are blocking all storage access requests."
      )
  );
  ok(true, "Third-party storage access blocked message was displayed");

  // We explicitely destroy the toolbox in order to ensure waiting for its full destruction
  // and avoid leak / pending requests
  await hud.toolbox.destroy();
  win.close();

  await extension.unload();
});

function getStorageErrorUrl(category) {
  const BASE_STORAGE_ERROR_URL =
    "https://developer.mozilla.org/docs/Web/Privacy/Guides/Storage_Access_Policy/Errors/";
  const STORAGE_ERROR_URL_PARAMS = new URLSearchParams({
    utm_source: "devtools",
    utm_medium: "firefox-cookie-errors",
    utm_campaign: "default",
  }).toString();
  return `${BASE_STORAGE_ERROR_URL}${category}?${STORAGE_ERROR_URL_PARAMS}`;
}

async function testLearnMoreClickOpenNewTab(message, expectedUrl) {
  info("Clicking on the Learn More link");

  const learnMoreLink = message.querySelector(".learn-more-link");
  const linkSimulation = await simulateLinkClick(learnMoreLink);
  checkLink({
    ...linkSimulation,
    expectedLink: expectedUrl,
    expectedTab: "tab",
  });
}

function checkLink({ link, where, expectedLink, expectedTab }) {
  is(link, expectedLink, `Clicking the provided link opens ${link}`);
  is(where, expectedTab, `Clicking the provided link opens in expected tab`);
}
