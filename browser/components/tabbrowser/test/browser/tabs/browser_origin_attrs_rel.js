/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from helper_origin_attrs_testing.js */
loadTestSubscript("helper_origin_attrs_testing.js");

const PATH =
  "browser/browser/components/tabbrowser/test/browser/tabs/file_rel_opener_noopener.html";
const URI_EXAMPLECOM =
  "https://example.com/browser/browser/components/tabbrowser/test/browser/tabs/blank.html";
const URI_EXAMPLEORG =
  "https://example.org/browser/browser/components/tabbrowser/test/browser/tabs/blank.html";
var TEST_CASES = ["https://example.com/" + PATH, "https://example.org/" + PATH];
// How many times we navigate (exclude going back)
const NUM_NAVIGATIONS = 5;
// Remote types we expect for all pages that we open, in the order of being opened
// (we don't include remote type for when we navigate back after clicking on a link)
var remoteTypes;
var xulFrameLoaderCreatedCounter = {};
var LINKS_INFO = [
  {
    uri: URI_EXAMPLECOM,
    id: "link_noopener_examplecom",
  },
  {
    uri: URI_EXAMPLECOM,
    id: "link_opener_examplecom",
  },
  {
    uri: URI_EXAMPLEORG,
    id: "link_noopener_exampleorg",
  },
  {
    uri: URI_EXAMPLEORG,
    id: "link_opener_exampleorg",
  },
];

function handleEventLocal(aEvent) {
  if (aEvent.type != "XULFrameLoaderCreated") {
    return;
  }
  // Ignore <browser> element in about:preferences and any other special pages
  if ("gBrowser" in aEvent.target.documentGlobal) {
    xulFrameLoaderCreatedCounter.numCalledSoFar++;
  }
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.wait300msAfterTabSwitch", true],
      ["privacy.userContext.enabled", true],
      // don't preload tabs so we don't have extra XULFrameLoaderCreated events
      // firing
      ["browser.newtab.preload", false],
    ],
  });
  requestLongerTimeout(3);
});

function setupRemoteTypes() {
  if (gFissionBrowser) {
    remoteTypes = {
      initial: [
        "webIsolated=https://example.com",
        "webIsolated=https://example.com^userContextId=1",
        "webIsolated=https://example.com^userContextId=2",
        "webIsolated=https://example.com^userContextId=3",
        "webIsolated=https://example.com^privateBrowsingId=1",
        "webIsolated=https://example.org",
        "webIsolated=https://example.org^userContextId=1",
        "webIsolated=https://example.org^userContextId=2",
        "webIsolated=https://example.org^userContextId=3",
        "webIsolated=https://example.org^privateBrowsingId=1",
      ],
      regular: {},
      1: {},
      2: {},
      3: {},
      private: {},
    };
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
    remoteTypes.private[URI_EXAMPLECOM] =
      "webIsolated=https://example.com^privateBrowsingId=1";
    remoteTypes.private[URI_EXAMPLEORG] =
      "webIsolated=https://example.org^privateBrowsingId=1";
  } else {
    let web = Array(NUM_NAVIGATIONS).fill("web");
    remoteTypes = {
      initial: [...web, ...web],
      regular: {},
      1: {},
      2: {},
      3: {},
      private: {},
    };
    remoteTypes.regular[URI_EXAMPLECOM] = "web";
    remoteTypes.regular[URI_EXAMPLEORG] = "web";
    remoteTypes["1"][URI_EXAMPLECOM] = "web";
    remoteTypes["1"][URI_EXAMPLEORG] = "web";
    remoteTypes["2"][URI_EXAMPLECOM] = "web";
    remoteTypes["2"][URI_EXAMPLEORG] = "web";
    remoteTypes["3"][URI_EXAMPLECOM] = "web";
    remoteTypes["3"][URI_EXAMPLEORG] = "web";
    remoteTypes.private[URI_EXAMPLECOM] = "web";
    remoteTypes.private[URI_EXAMPLEORG] = "web";
  }
}

add_task(async function test_user_identity_simple() {
  setupRemoteTypes();
  /**
   * For each test case
   * - open regular, private and container tabs and load uri
   * - in all the tabs, click on 4 links, going back each time in between clicks
   *     and verifying the remote type stays the same throughout
   * - close tabs
   */

  for (var idx = 0; idx < TEST_CASES.length; idx++) {
    var uri = TEST_CASES[idx];
    info(`Will open ${uri} in different tabs`);

    // Open uri without a container
    let page_regular = await openURIInRegularTab(uri);
    is(
      page_regular.tab.linkedBrowser.remoteType,
      remoteTypes.initial.shift(),
      "correct remote type"
    );

    let pages_usercontexts = [];
    for (
      var user_context_id = 1;
      user_context_id <= NUM_USER_CONTEXTS;
      user_context_id++
    ) {
      let containerPage = await openURIInContainer(
        uri,
        window,
        user_context_id.toString()
      );
      is(
        containerPage.tab.linkedBrowser.remoteType,
        remoteTypes.initial.shift(),
        "correct remote type"
      );
      pages_usercontexts.push(containerPage);
    }

    // Open the same uri in a private browser
    let page_private = await openURIInPrivateTab(uri);
    is(
      page_private.tab.linkedBrowser.remoteType,
      remoteTypes.initial.shift(),
      "correct remote type"
    );

    info(`Opened initial set of pages`);

    for (const linkInfo of LINKS_INFO) {
      info(
        `Will make all tabs click on link ${linkInfo.uri} id ${linkInfo.id}`
      );
      info(`Will click on link ${linkInfo.uri} in regular tab`);
      await clickOnLink(
        page_regular.tab.linkedBrowser,
        uri,
        linkInfo,
        "regular"
      );

      info(`Will click on link ${linkInfo.uri} in private tab`);
      await clickOnLink(
        page_private.tab.linkedBrowser,
        uri,
        linkInfo,
        "private"
      );

      for (const page of pages_usercontexts) {
        info(
          `Will click on link ${linkInfo.uri} in container ${page.user_context_id}`
        );
        await clickOnLink(
          page.tab.linkedBrowser,
          uri,
          linkInfo,
          page.user_context_id.toString()
        );
      }
    }

    // Close all the tabs
    pages_usercontexts.forEach(page => {
      BrowserTestUtils.removeTab(page.tab);
    });
    BrowserTestUtils.removeTab(page_regular.tab);
    BrowserTestUtils.removeTab(page_private.tab);
  }
});

async function clickOnLink(aBrowser, aCurrURI, aLinkInfo, aIdxForRemoteTypes) {
  var remoteTypeBeforeNavigation = aBrowser.remoteType;
  var currRemoteType;

  // Add a listener
  initXulFrameLoaderCreatedCounter(xulFrameLoaderCreatedCounter);
  aBrowser.documentGlobal.gBrowser.addEventListener(
    "XULFrameLoaderCreated",
    handleEventLocal
  );

  // Retrieve the expected remote type
  var expectedRemoteType = remoteTypes[aIdxForRemoteTypes][aLinkInfo.uri];

  // Click on the link
  info(`Clicking on link, expected remote type= ${expectedRemoteType}`);
  let newTabLoaded = BrowserTestUtils.waitForNewTab(
    aBrowser.documentGlobal.gBrowser,
    aLinkInfo.uri,
    true
  );
  SpecialPowers.spawn(aBrowser, [aLinkInfo.id], link_id => {
    content.document.getElementById(link_id).click();
  });

  // Wait for the new tab to be opened
  info(`About to wait for the clicked link to load in browser`);
  let newTab = await newTabLoaded;

  // Check remote type, once we have opened a new tab
  info(`Finished waiting for the clicked link to load in browser`);
  currRemoteType = newTab.linkedBrowser.remoteType;
  is(currRemoteType, expectedRemoteType, "Got correct remote type");

  // Verify firing of XULFrameLoaderCreated event
  info(
    `XULFrameLoaderCreated was fired ${xulFrameLoaderCreatedCounter.numCalledSoFar}
     time(s) for tab ${aIdxForRemoteTypes} when clicking on ${aLinkInfo.id} from page ${aCurrURI}`
  );
  var numExpected;
  if (!gFissionBrowser && aLinkInfo.id.includes("noopener")) {
    numExpected = 1;
  } else {
    numExpected = currRemoteType == remoteTypeBeforeNavigation ? 1 : 2;
  }
  info(
    `num XULFrameLoaderCreated events expected ${numExpected}, curr ${currRemoteType} prev ${remoteTypeBeforeNavigation}`
  );
  is(
    xulFrameLoaderCreatedCounter.numCalledSoFar,
    numExpected,
    `XULFrameLoaderCreated was fired ${xulFrameLoaderCreatedCounter.numCalledSoFar}
     time(s) for tab ${aIdxForRemoteTypes} when clicking on ${aLinkInfo.id} from page ${aCurrURI}`
  );

  // Remove the event listener
  aBrowser.documentGlobal.gBrowser.removeEventListener(
    "XULFrameLoaderCreated",
    handleEventLocal
  );

  BrowserTestUtils.removeTab(newTab);
}
