/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * Test that the network monitor displays all requests in an HTTP authentication
 * flow.
 */

const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

const AUTH_SJS = EXAMPLE_URL + "sjs_auth-test-server.sjs";
const USERNAME = "guest";
const PASSWORD = "guest";

/**
 * Return a unique URL for the auth server to avoid cached auth responses
 * causing a different number of network events across test runs.
 */
function getUniqueAuthURL() {
  const uuid = Services.uuid.generateUUID().number.slice(1, -1);
  return AUTH_SJS + "?" + uuid;
}

function cleanupAuthManager() {
  const authManager = SpecialPowers.Cc[
    "@mozilla.org/network/http-auth-manager;1"
  ].getService(SpecialPowers.Ci.nsIHttpAuthManager);
  authManager.clearAll();
}

add_task(async function testAuthRequestsDisplayedSeparately() {
  cleanupAuthManager();

  // Bug 2038321: At the moment devtools only displays distinct requests for
  // each authentication attempt when the following pref is true.
  await pushPref("network.auth.use_redirect_for_retries", true);

  const { monitor, tab } = await initNetMonitor(SIMPLE_URL, {
    requestCount: 1,
  });
  info("Starting test...");

  const { store, windowRequire } = monitor.panelWin;
  const Actions = windowRequire("devtools/client/netmonitor/src/actions/index");
  const { getSortedRequests } = windowRequire(
    "devtools/client/netmonitor/src/selectors/index"
  );
  store.dispatch(Actions.batchEnable(false));

  const authURL = getUniqueAuthURL();

  // Set up the auth dialog handler before triggering the fetch so we don't
  // miss the prompt.
  const onAuthHandled = PromptTestUtils.handleNextPrompt(
    tab.linkedBrowser,
    {
      promptType: "promptUserAndPass",
      modalType: Services.prompt.MODAL_TYPE_TAB,
    },
    { buttonNumClick: 0, loginInput: USERNAME, passwordInput: PASSWORD }
  );

  info("Fetching auth-protected URL to trigger the authentication flow");
  await SpecialPowers.spawn(tab.linkedBrowser, [authURL], url => {
    content.wrappedJSObject.fetch(url);
  });

  info("Waiting for the auth dialog to be handled");
  await onAuthHandled;

  info("Waiting for the 200 response to appear in the network monitor");
  await waitUntil(() =>
    store.getState().requests.requests.some(r => r.status === "200")
  );
  await waitForAllNetworkUpdateEvents();

  is(
    store.getState().requests.requests.length,
    2,
    "Both the 401 and the 200 auth requests should be visible in the network monitor"
  );

  const requests = getSortedRequests(store.getState());
  is(requests[0].status, "401", "The first request should have status 401");
  is(requests[1].status, "200", "The second request should have status 200");

  await teardown(monitor);
});
