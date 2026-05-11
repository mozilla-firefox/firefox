/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/* import-globals-from helper_origin_attrs_testing.js */
loadTestSubscript("helper_origin_attrs_testing.js");

const PATH =
  "browser/browser/components/tabbrowser/test/browser/tabs/blank.html";

var TEST_CASES = [
  { uri: "https://example.com/" + PATH },
  { uri: "https://example.org/" + PATH },
  { uri: "about:preferences" },
  { uri: "about:config" },
  // file:// uri will be added in setup()
];

// 3 container tabs, 1 regular tab and 1 private tab
const NUM_PAGES_OPEN_FOR_EACH_TEST_CASE = 5;
var remoteTypes;

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
  requestLongerTimeout(5);

  // Add a file:// uri
  let dir = getChromeDir(getResolvedURI(gTestPath));
  dir.append("blank.html");
  // The file can be a symbolic link on local build.  Normalize it to make sure
  // the path matches to the actual URI opened in the new tab.
  dir.normalize();
  const uriString = Services.io.newFileURI(dir).spec;
  TEST_CASES.push({ uri: uriString });
});

function setupRemoteTypes() {
  remoteTypes = getExpectedRemoteTypes(
    gFissionBrowser,
    NUM_PAGES_OPEN_FOR_EACH_TEST_CASE
  );
  remoteTypes = remoteTypes.concat(
    Array(NUM_PAGES_OPEN_FOR_EACH_TEST_CASE).fill("file")
  ); // file uri
}

add_task(async function test_user_identity_simple() {
  setupRemoteTypes();
  var currentRemoteType;

  for (let testData of TEST_CASES) {
    info(`Will open ${testData.uri} in different tabs`);
    // Open uri without a container
    info(`About to open a regular page`);
    currentRemoteType = remoteTypes.shift();
    let page_regular = await openURIInRegularTab(testData.uri, window);
    is(
      page_regular.tab.linkedBrowser.remoteType,
      currentRemoteType,
      "correct remote type"
    );

    // Open the same uri in different user contexts
    info(`About to open container pages`);
    let containerPages = [];
    for (
      var user_context_id = 1;
      user_context_id <= NUM_USER_CONTEXTS;
      user_context_id++
    ) {
      currentRemoteType = remoteTypes.shift();
      let containerPage = await openURIInContainer(
        testData.uri,
        window,
        user_context_id
      );
      is(
        containerPage.tab.linkedBrowser.remoteType,
        currentRemoteType,
        "correct remote type"
      );
      containerPages.push(containerPage);
    }

    // Open the same uri in a private browser
    currentRemoteType = remoteTypes.shift();
    let page_private = await openURIInPrivateTab(testData.uri);
    let privateRemoteType = page_private.tab.linkedBrowser.remoteType;
    is(privateRemoteType, currentRemoteType, "correct remote type");

    // Close all the tabs
    containerPages.forEach(page => {
      BrowserTestUtils.removeTab(page.tab);
    });
    BrowserTestUtils.removeTab(page_regular.tab);
    BrowserTestUtils.removeTab(page_private.tab);
  }
});
