/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const NUM_USER_CONTEXTS = 3;

var xulFrameLoaderCreatedListenerInfo;

function initXulFrameLoaderListenerInfo() {
  xulFrameLoaderCreatedListenerInfo = {};
  xulFrameLoaderCreatedListenerInfo.numCalledSoFar = 0;
}

function handleEvent(aEvent) {
  if (aEvent.type != "XULFrameLoaderCreated") {
    return;
  }
  // Ignore <browser> element in about:preferences and any other special pages
  if ("gBrowser" in aEvent.target.documentGlobal) {
    xulFrameLoaderCreatedListenerInfo.numCalledSoFar++;
  }
}

async function openURIInRegularTab(uri, win = window) {
  info(`Opening url ${uri} in a regular tab`);

  initXulFrameLoaderListenerInfo();
  win.gBrowser.addEventListener("XULFrameLoaderCreated", handleEvent);

  let tab = await BrowserTestUtils.openNewForegroundTab(win.gBrowser, uri);
  info(
    `XULFrameLoaderCreated was fired ${xulFrameLoaderCreatedListenerInfo.numCalledSoFar} time(s) for ${uri} in regular tab`
  );

  is(
    xulFrameLoaderCreatedListenerInfo.numCalledSoFar,
    1,
    "XULFrameLoaderCreated fired correct number of times"
  );

  win.gBrowser.removeEventListener("XULFrameLoaderCreated", handleEvent);
  return { tab, uri };
}

async function openURIInContainer(uri, win, userContextId) {
  info(`Opening url ${uri} in user context ${userContextId}`);
  initXulFrameLoaderListenerInfo();
  win.gBrowser.addEventListener("XULFrameLoaderCreated", handleEvent);

  let tab = BrowserTestUtils.addTab(win.gBrowser, uri, {
    userContextId,
  });
  is(
    tab.getAttribute("usercontextid"),
    userContextId.toString(),
    "New tab has correct user context id"
  );

  let browser = tab.linkedBrowser;

  await BrowserTestUtils.browserLoaded(browser, false, uri);
  info(
    `XULFrameLoaderCreated was fired ${xulFrameLoaderCreatedListenerInfo.numCalledSoFar}
     time(s) for ${uri} in container tab ${userContextId}`
  );

  is(
    xulFrameLoaderCreatedListenerInfo.numCalledSoFar,
    1,
    "XULFrameLoaderCreated fired correct number of times"
  );

  win.gBrowser.removeEventListener("XULFrameLoaderCreated", handleEvent);

  return { tab, user_context_id: userContextId, uri };
}

async function openURIInPrivateTab(uri) {
  info(
    `Opening url ${
      uri ? uri : "about:privatebrowsing"
    } in a private browsing tab`
  );
  let win = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
    waitForTabURL: "about:privatebrowsing",
  });
  if (!uri) {
    return { tab: win.gBrowser.selectedTab, uri: "about:privatebrowsing" };
  }
  initXulFrameLoaderListenerInfo();
  win.gBrowser.addEventListener("XULFrameLoaderCreated", handleEvent);

  const browser = win.gBrowser.selectedTab.linkedBrowser;
  let prevRemoteType = browser.remoteType;
  let loaded = BrowserTestUtils.browserLoaded(browser, false, uri);
  BrowserTestUtils.startLoadingURIString(browser, uri);
  await loaded;
  let currRemoteType = browser.remoteType;

  info(
    `XULFrameLoaderCreated was fired ${xulFrameLoaderCreatedListenerInfo.numCalledSoFar} time(s) for ${uri} in private tab`
  );

  if (currRemoteType == prevRemoteType && uri == "about:blank") {
    // about:blank page gets flagged for being eligible to go into bfcache
    // and thus we create a new XULFrameLoader for these pages
    is(
      xulFrameLoaderCreatedListenerInfo.numCalledSoFar,
      1,
      "XULFrameLoaderCreated fired correct number of times"
    );
  } else {
    is(
      xulFrameLoaderCreatedListenerInfo.numCalledSoFar,
      currRemoteType == prevRemoteType ? 0 : 1,
      "XULFrameLoaderCreated fired correct number of times"
    );
  }

  win.gBrowser.removeEventListener("XULFrameLoaderCreated", handleEvent);
  return { tab: win.gBrowser.selectedTab, uri };
}

function initXulFrameLoaderCreatedCounter(aXulFrameLoaderCreatedListenerInfo) {
  aXulFrameLoaderCreatedListenerInfo.numCalledSoFar = 0;
}

// Expected remote types for the following tests:
// browser/components/tabbrowser/test/browser/tabs/browser_navigate_through_urls_origin_attributes.js
// browser/components/tabbrowser/test/browser/tabs/browser_origin_attrs_in_remote_type.js
function getExpectedRemoteTypes(gFissionBrowser, numPagesOpen) {
  var remoteTypes;
  if (gFissionBrowser) {
    remoteTypes = [
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
    ];
  } else {
    remoteTypes = Array(numPagesOpen * 2).fill("web"); // example.com and example.org
  }
  remoteTypes = remoteTypes.concat(Array(numPagesOpen * 2).fill(null)); // about: pages
  return remoteTypes;
}
