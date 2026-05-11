/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from helper_origin_attrs_testing.js */
loadTestSubscript("helper_origin_attrs_testing.js");

const PATH =
  "browser/browser/components/tabbrowser/test/browser/tabs/blank.html";

var TEST_CASES = [
  { uri: "https://example.com/" + PATH },
  { uri: "https://example.org/" + PATH },
  { uri: "about:preferences" },
  { uri: "about:config" },
];

// 3 container tabs, 1 regular tab and 1 private tab
const NUM_PAGES_OPEN_FOR_EACH_TEST_CASE = 5;
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

var gPrevRemoteTypeRegularTab;
var gPrevRemoteTypeContainerTab;
var gPrevRemoteTypePrivateTab;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["privacy.userContext.enabled", true],
      // don't preload tabs so we don't have extra XULFrameLoaderCreated events
      // firing
      ["browser.newtab.preload", false],
    ],
  });

  requestLongerTimeout(4);
});

function setupRemoteTypes() {
  gPrevRemoteTypeRegularTab = null;
  gPrevRemoteTypeContainerTab = {};
  gPrevRemoteTypePrivateTab = null;

  remoteTypes = getExpectedRemoteTypes(
    gFissionBrowser,
    NUM_PAGES_OPEN_FOR_EACH_TEST_CASE
  );
}

add_task(async function testNavigate() {
  setupRemoteTypes();
  /**
   * Open a regular tab, 3 container tabs and a private window, load about:blank or about:privatebrowsing
   * For each test case
   *  load the uri
   *  verify correct remote type
   * close tabs
   */

  let regularPage = await openURIInRegularTab("about:blank", window);
  gPrevRemoteTypeRegularTab = regularPage.tab.linkedBrowser.remoteType;
  let containerPages = [];
  for (
    var user_context_id = 1;
    user_context_id <= NUM_USER_CONTEXTS;
    user_context_id++
  ) {
    let containerPage = await openURIInContainer(
      "about:blank",
      window,
      user_context_id
    );
    gPrevRemoteTypeContainerTab[user_context_id] =
      containerPage.tab.linkedBrowser.remoteType;
    containerPages.push(containerPage);
  }

  let privatePage = await openURIInPrivateTab();
  gPrevRemoteTypePrivateTab = privatePage.tab.linkedBrowser.remoteType;

  for (const testCase of TEST_CASES) {
    let uri = testCase.uri;

    await loadURIAndCheckRemoteType(
      regularPage.tab.linkedBrowser,
      uri,
      "regular tab",
      gPrevRemoteTypeRegularTab
    );
    gPrevRemoteTypeRegularTab = regularPage.tab.linkedBrowser.remoteType;

    for (const page of containerPages) {
      await loadURIAndCheckRemoteType(
        page.tab.linkedBrowser,
        uri,
        `container tab ${page.user_context_id}`,
        gPrevRemoteTypeContainerTab[page.user_context_id]
      );
      gPrevRemoteTypeContainerTab[page.user_context_id] =
        page.tab.linkedBrowser.remoteType;
    }

    await loadURIAndCheckRemoteType(
      privatePage.tab.linkedBrowser,
      uri,
      "private tab",
      gPrevRemoteTypePrivateTab
    );
    gPrevRemoteTypePrivateTab = privatePage.tab.linkedBrowser.remoteType;
  }
  // Close tabs
  containerPages.forEach(containerPage => {
    BrowserTestUtils.removeTab(containerPage.tab);
  });
  BrowserTestUtils.removeTab(regularPage.tab);
  BrowserTestUtils.removeTab(privatePage.tab);
});

async function loadURIAndCheckRemoteType(
  aBrowser,
  aURI,
  aText,
  aPrevRemoteType
) {
  let expectedCurr = remoteTypes.shift();
  initXulFrameLoaderCreatedCounter(xulFrameLoaderCreatedCounter);
  aBrowser.documentGlobal.gBrowser.addEventListener(
    "XULFrameLoaderCreated",
    handleEventLocal
  );
  let loaded = BrowserTestUtils.browserLoaded(aBrowser, false, aURI);
  info(`About to load ${aURI} in ${aText}`);
  BrowserTestUtils.startLoadingURIString(aBrowser, aURI);
  await loaded;

  // Verify correct remote type
  is(
    expectedCurr,
    aBrowser.remoteType,
    `correct remote type for ${aURI} ${aText}`
  );

  // Verify XULFrameLoaderCreated firing correct number of times
  info(
    `XULFrameLoaderCreated was fired ${xulFrameLoaderCreatedCounter.numCalledSoFar} time(s) for ${aURI} ${aText}`
  );

  // With BFCache in the parent we'll get a XULFrameLoaderCreated even if
  // expectedCurr == aPrevRemoteType, because we store the old frameloader in
  // the BFCache. We have to make an exception for loads in the parent process
  // (which have a null aPrevRemoteType/expectedCurr) because BFCache in the
  // parent disables caching for those loads.
  var numExpected = expectedCurr == aPrevRemoteType && !expectedCurr ? 0 : 1;
  is(
    xulFrameLoaderCreatedCounter.numCalledSoFar,
    numExpected,
    `XULFrameLoaderCreated fired correct number of times for ${aURI} ${aText} 
    prev=${aPrevRemoteType} curr =${aBrowser.remoteType}`
  );
  aBrowser.documentGlobal.gBrowser.removeEventListener(
    "XULFrameLoaderCreated",
    handleEventLocal
  );
}
