/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from ../../../tabbrowser/test/browser/tabs/helper_origin_attrs_testing.js */
loadTestSubscript(
  "../../../tabbrowser/test/browser/tabs/helper_origin_attrs_testing.js"
);

const PATH =
  "/browser/browser/components/contextualidentity/test/browser/blank.html";

const URI_EXAMPLECOM = "https://example.com/" + PATH;
const URI_EXAMPLEORG = "https://example.org/" + PATH;

var TEST_CASES = [
  URI_EXAMPLECOM,
  URI_EXAMPLEORG,
  "about:preferences",
  "about:config",
];

var remoteTypes;

var xulFrameLoaderCreatedCounter = {};

function handleEventLocal(aEvent) {
  if (aEvent.type != "XULFrameLoaderCreated") {
    return;
  }
  // Ignore <browser> element in about:preferences and any other special pages
  if ("gBrowser" in aEvent.target.documentGlobal) {
    xulFrameLoaderCreatedCounter.numCalledSoFar++;
  }
}
const NUM_PAGES_OPEN_FOR_EACH_TEST_CASE = 5;
add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.userContext.enabled", true],
      // don't preload tabs so we don't have extra XULFrameLoaderCreated events
      // firing
      ["browser.newtab.preload", false],
    ],
  });

  requestLongerTimeout(5);
});

function setupRemoteTypes() {
  remoteTypes = {
    regular: { "about:preferences": null, "about:config": null },
    1: { "about:preferences": null, "about:config": null },
    2: { "about:preferences": null, "about:config": null },
    3: { "about:preferences": null, "about:config": null },
  };
  if (gFissionBrowser) {
    remoteTypes.regular[URI_EXAMPLECOM] = "webIsolated=https://example.com";
    remoteTypes.regular[URI_EXAMPLEORG] = "webIsolated=https://example.org";
    remoteTypes["1"][URI_EXAMPLECOM] =
      "webIsolated=https://example.com^userContextId=1";
    remoteTypes["1"][URI_EXAMPLEORG] =
      "webIsolated=https://example.org^userContextId=1";
    remoteTypes["2"][URI_EXAMPLECOM] =
      "webIsolated=https://example.com^userContextId=2";
    remoteTypes["2"][URI_EXAMPLEORG] =
      "webIsolated=https://example.org^userContextId=2";
    remoteTypes["3"][URI_EXAMPLECOM] =
      "webIsolated=https://example.com^userContextId=3";
    remoteTypes["3"][URI_EXAMPLEORG] =
      "webIsolated=https://example.org^userContextId=3";
  } else {
    remoteTypes.regular[URI_EXAMPLECOM] = "web";
    remoteTypes.regular[URI_EXAMPLEORG] = "web";
    remoteTypes["1"][URI_EXAMPLECOM] = "web";
    remoteTypes["1"][URI_EXAMPLEORG] = "web";
    remoteTypes["2"][URI_EXAMPLECOM] = "web";
    remoteTypes["2"][URI_EXAMPLEORG] = "web";
    remoteTypes["3"][URI_EXAMPLECOM] = "web";
    remoteTypes["3"][URI_EXAMPLEORG] = "web";
  }
}

add_task(async function testReopen() {
  setupRemoteTypes();
  /**
   * Open a regular tab
   * For each url
   *  navigate regular tab to that url
   *  pid_seen = [regular tab pid]
   *  For each container
   *    reopen regular tab in that container
   *    verify remote type
   *    reopen container tab in regular tab
   *  Close all the tabs
   * This tests that behaviour of reopening tabs is correct
   */

  let regularPage = await openURIInRegularTab("about:blank");
  var currRemoteType;

  for (const uri of TEST_CASES) {
    // Navigate regular tab to a test-uri
    let loaded = BrowserTestUtils.browserLoaded(
      regularPage.tab.linkedBrowser,
      false,
      uri
    );
    BrowserTestUtils.startLoadingURIString(regularPage.tab.linkedBrowser, uri);
    await loaded;
    info(`Start Opened ${uri} in a regular tab`);
    currRemoteType = regularPage.tab.linkedBrowser.remoteType;
    is(currRemoteType, remoteTypes.regular[uri], "correct remote type");

    let containerTabs = [];

    // Reopen this test-uri in different containers and ensure pids are different
    for (var userCtxId = 1; userCtxId <= NUM_USER_CONTEXTS; userCtxId++) {
      // Prepare the reopen menu
      let reopenMenu = await openReopenMenuForTab(regularPage.tab);

      // Add a listener for XULFrameLoaderCreated
      initXulFrameLoaderCreatedCounter(xulFrameLoaderCreatedCounter);
      regularPage.tab.documentGlobal.gBrowser.addEventListener(
        "XULFrameLoaderCreated",
        handleEventLocal
      );

      // Reopen the page in a container
      let containerTab = await openTabInContainer(
        gBrowser,
        uri,
        reopenMenu,
        userCtxId.toString()
      );
      info(`Re-opened ${uri} in a container tab ${userCtxId}`);

      // Check correct remote type
      currRemoteType = containerTab.linkedBrowser.remoteType;
      is(
        currRemoteType,
        remoteTypes[userCtxId.toString()][uri],
        "correct remote type"
      );

      // Check that XULFrameLoaderCreated has fired off correct number of times
      is(
        xulFrameLoaderCreatedCounter.numCalledSoFar,
        1,
        `XULFrameLoaderCreated was fired once when reopening ${uri} in container ${userCtxId}`
      );
      regularPage.tab.documentGlobal.gBrowser.removeEventListener(
        "XULFrameLoaderCreated",
        handleEventLocal
      );

      // Save the tab so we can close it later
      containerTabs.push(containerTab);
    }
    let containedTabsOpenedInNoContainer = [];

    for (const containerTab of containerTabs) {
      info(`About to re-open ${uri} in a regular, no-container tab`);
      let reopenMenu = await openReopenMenuForTab(containerTab);
      let reopenedInNoContainerTab = await openTabInContainer(
        gBrowser,
        uri,
        reopenMenu,
        "0"
      );
      info(`Re-opened ${uri} in a regular, no-container tab`);
      // Check remote type
      currRemoteType = reopenedInNoContainerTab.linkedBrowser.remoteType;
      is(currRemoteType, remoteTypes.regular[uri], "correct remote type");
      containedTabsOpenedInNoContainer.push(reopenedInNoContainerTab);
    }

    // Close tabs
    for (const tab of containerTabs.concat(containedTabsOpenedInNoContainer)) {
      BrowserTestUtils.removeTab(tab);
    }
  }
  BrowserTestUtils.removeTab(regularPage.tab);
});
