"use strict";

SimpleTest.requestCompleteLog();
loadTestSubscript("head_sessions.js");
const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

async function openAndCloseTab(window, url) {
  let tab = BrowserTestUtils.addTab(window.gBrowser, url);
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, true, url);
  await TabStateFlusher.flush(tab.linkedBrowser);
  let sessionUpdatePromise = BrowserTestUtils.waitForSessionStoreUpdate(tab);
  BrowserTestUtils.removeTab(tab);
  await sessionUpdatePromise;
}

async function run_test_extension(incognitoOverride, testData) {
  const initialURL = gBrowser.selectedBrowser.currentURI.spec;
  // We'll be closing tabs from a private window and a non-private window and attempting
  // to call session.restore() at each step. The goal is to compare the actual and
  // expected outcome when the extension is/isn't configured for incognito window use.

  function background() {
    browser.test.onMessage.addListener(async (msg, sessionId) => {
      let result;
      try {
        result = await browser.sessions.restore(sessionId);
      } catch (e) {
        result = { error: e.message };
      }
      browser.test.sendMessage("result", result);
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["sessions", "tabs"],
    },
    background,
    incognitoOverride,
  });
  await extension.startup();

  // Open a private browsing window and with a non-empty tab
  // (so we dont end up closing the window when the close the other tab)
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  await BrowserTestUtils.openNewForegroundTab(
    privateWin.gBrowser,
    testData.private.initialTabURL
  );

  // open and close a tab in the non-private window
  await openAndCloseTab(window, testData.notPrivate.tabToClose);

  let { closedId: nonPrivateClosedTabId, pos: nonPrivateIndex } =
    SessionStore.getClosedTabDataForWindow(window)[0];
  if (!testData.notPrivate.expected.error) {
    testData.notPrivate.expected.index = nonPrivateIndex;
  }

  // open and close a tab in the private window
  info(
    "open & close a tab in the private window with URL: " +
      testData.private.tabToClose
  );
  await openAndCloseTab(privateWin, testData.private.tabToClose);
  let { pos: privateIndex } =
    SessionStore.getClosedTabDataForWindow(privateWin)[0];
  if (!testData.private.expected.error) {
    testData.private.expected.index = privateIndex;
  }

  // focus the non-private window to ensure the outcome isn't just a side-effect of the
  // private window being the top window
  await SimpleTest.promiseFocus(window);

  // Try to restore the last-closed tab - which was private.
  // If incognito access is allowed, we should successfully restore it to the private window.
  // We pass no closedId so it should just try to restore the last closed tab
  info("Sending 'restore' to attempt restore the closed private tab");
  extension.sendMessage("restore");
  let sessionStoreChanged = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  let extResult = await extension.awaitMessage("result");
  let result = {};
  if (extResult.tab) {
    await sessionStoreChanged;
    // session.restore() was returning "about:blank" as the tab.url,
    // we'll wait to ensure the correct URL eventually loads in the restored tab
    await BrowserTestUtils.browserLoaded(
      privateWin.gBrowser.selectedTab.linkedBrowser,
      true,
      testData.private.tabToClose
    );
    // only keep the properties we want to compare
    for (let pname of Object.keys(testData.private.expected)) {
      result[pname] = extResult.tab[pname];
    }
    result.url = privateWin.gBrowser.selectedTab.linkedBrowser.currentURI.spec;
  } else {
    // Trim off the sessionId value so we can easily equality-match on the result
    result.error = extResult.error.replace(/sessionId\s+\d+/, "sessionId");
  }
  Assert.deepEqual(
    result,
    testData.private.expected,
    "Restoring the private tab didn't match expected result"
  );

  await SimpleTest.promiseFocus(privateWin);

  // Try to restore the last-closed tab in the non-private window
  info("Sending 'restore' to restore the non-private tab");
  extension.sendMessage("restore", String(nonPrivateClosedTabId));
  sessionStoreChanged = TestUtils.topicObserved(
    "sessionstore-closed-objects-changed"
  );
  extResult = await extension.awaitMessage("result");
  result = {};

  if (extResult.tab) {
    await sessionStoreChanged;
    await BrowserTestUtils.browserLoaded(
      window.gBrowser.selectedTab.linkedBrowser,
      true,
      testData.notPrivate.tabToClose
    );
    // only keep the properties we want to compare
    for (let pname of Object.keys(testData.notPrivate.expected)) {
      result[pname] = extResult.tab[pname];
    }
    result.url = window.gBrowser.selectedTab.linkedBrowser.currentURI.spec;
  } else {
    // Trim off the sessionId value so we can easily equality-match on the result
    result.error = extResult.error.replace(/sessionId\s+\d+/, "sessionId");
  }
  Assert.deepEqual(
    result,
    testData.notPrivate.expected,
    "Restoring the non-private tab didn't match expected result"
  );

  // Close the private window and cleanup
  await BrowserTestUtils.closeWindow(privateWin);
  for (let tab of gBrowser.tabs.filter(
    tab => !tab.hidden && tab.linkedBrowser.currentURI.spec !== initialURL
  )) {
    await BrowserTestUtils.removeTab(tab);
  }
  await extension.unload();
}

const spanningTestData = {
  private: {
    initialTabURL: "https://example.com/",
    tabToClose: "https://example.org/?private",
    // restore should succeed when incognito is allowed
    expected: {
      url: "https://example.org/?private",
      incognito: true,
    },
  },
  notPrivate: {
    initialTabURL: "https://example.com/",
    tabToClose: "https://example.org/?notprivate",
    expected: {
      url: "https://example.org/?notprivate",
      incognito: false,
    },
  },
};

add_task(
  async function test_sessions_get_recently_closed_private_incognito_spanning() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.sessionstore.closedTabsFromAllWindows", true]],
    });
    await run_test_extension("spanning", spanningTestData);
    SpecialPowers.popPrefEnv();
  }
);
add_task(
  async function test_sessions_get_recently_closed_private_incognito_spanning_pref_off() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.sessionstore.closedTabsFromAllWindows", false]],
    });
    await run_test_extension("spanning", spanningTestData);
    SpecialPowers.popPrefEnv();
  }
);

const notAllowedTestData = {
  private: {
    initialTabURL: "https://example.com/",
    tabToClose: "https://example.org/?private",
    // this is expected to fail when incognito is not_allowed
    expected: {
      error: "Could not restore object using sessionId.",
    },
  },
  notPrivate: {
    // we'll open tabs for each URL
    initialTabURL: "https://example.com/",
    tabToClose: "https://example.org/?notprivate",
    expected: {
      url: "https://example.org/?notprivate",
      incognito: false,
    },
  },
};

add_task(
  async function test_sessions_get_recently_closed_private_incognito_not_allowed() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.sessionstore.closedTabsFromAllWindows", true]],
    });
    await run_test_extension("not_allowed", notAllowedTestData);
    SpecialPowers.popPrefEnv();
  }
);

add_task(
  async function test_sessions_get_recently_closed_private_incognito_not_allowed_pref_off() {
    await SpecialPowers.pushPrefEnv({
      set: [["browser.sessionstore.closedTabsFromAllWindows", false]],
    });
    await run_test_extension("not_allowed", notAllowedTestData);
    SpecialPowers.popPrefEnv();
  }
);
