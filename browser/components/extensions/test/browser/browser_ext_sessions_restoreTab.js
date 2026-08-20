"use strict";

ChromeUtils.defineESModuleGetters(this, {
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
});

/**
 This test checks that after closing an extension made tab it restores correctly.
 The tab is given an expanded triggering principal and we didn't use to serialize
 these correctly into session history.
 */

// Check that we can restore a tab modified by an extension.
add_task(async function test_restoringModifiedTab() {
  function background() {
    browser.tabs.create({ url: "https://example.com/" });
    browser.test.onMessage.addListener(msg => {
      if (msg == "change-tab") {
        browser.tabs.executeScript({ code: 'location.href += "?changedTab";' });
      }
    });
  }
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs", "<all_urls>"],
    },
    browser_action: {
      default_title: "Navigate current tab via content script",
    },
    background,
  });

  const contentScriptTabURL = "https://example.com/?changedTab";

  let win = await BrowserTestUtils.openNewBrowserWindow({});

  // Open and close a tabs.
  let tabPromise = BrowserTestUtils.waitForNewTab(
    win.gBrowser,
    "https://example.com/",
    true
  );
  await extension.startup();
  let firstTab = await tabPromise;
  let locationChange = BrowserTestUtils.waitForLocationChange(
    win.gBrowser,
    contentScriptTabURL
  );
  extension.sendMessage("change-tab");
  await locationChange;
  is(
    firstTab.linkedBrowser.currentURI.spec,
    contentScriptTabURL,
    "Got expected URL"
  );

  let sessionPromise = BrowserTestUtils.waitForSessionStoreUpdate(firstTab);
  BrowserTestUtils.removeTab(firstTab);
  await sessionPromise;

  tabPromise = BrowserTestUtils.waitForNewTab(
    win.gBrowser,
    contentScriptTabURL,
    true
  );
  SessionStore.undoCloseTab(win, 0);
  let restoredTab = await tabPromise;
  ok(restoredTab, "We returned a tab here");
  is(
    restoredTab.linkedBrowser.currentURI.spec,
    contentScriptTabURL,
    "Got expected URL"
  );

  await extension.unload();
  BrowserTestUtils.removeTab(restoredTab);

  // Close the window.
  await BrowserTestUtils.closeWindow(win);
});

add_task(async function test_restoringClosedTabWithTooLargeIndex() {
  function background() {
    browser.test.onMessage.addListener(async msg => {
      if (msg != "restoreTab") {
        return;
      }
      const recentlyClosed = await browser.sessions.getRecentlyClosed({
        maxResults: 2,
      });
      let tabWithTooLargeIndex;
      for (const info of recentlyClosed) {
        if (info.tab && info.tab.index > 1) {
          tabWithTooLargeIndex = info.tab;
          break;
        }
      }
      const onRestored = tab => {
        browser.tabs.onCreated.removeListener(onRestored);
        browser.test.sendMessage("restoredTab", tab);
      };
      browser.tabs.onCreated.addListener(onRestored);
      browser.sessions.restore(tabWithTooLargeIndex.sessionId);
    });
  }
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs", "sessions"],
    },
    background,
  });

  const win = await BrowserTestUtils.openNewBrowserWindow({});
  const tabs = await Promise.all([
    BrowserTestUtils.openNewForegroundTab(win.gBrowser, "about:blank?0"),
    BrowserTestUtils.openNewForegroundTab(win.gBrowser, "about:blank?1"),
  ]);
  const promsiedSessionStored = Promise.all([
    BrowserTestUtils.waitForSessionStoreUpdate(tabs[0]),
    BrowserTestUtils.waitForSessionStoreUpdate(tabs[1]),
  ]);
  // Close the rightmost tab at first
  BrowserTestUtils.removeTab(tabs[1]);
  BrowserTestUtils.removeTab(tabs[0]);
  await promsiedSessionStored;

  await extension.startup();
  const promisedRestoredTab = extension.awaitMessage("restoredTab");
  extension.sendMessage("restoreTab");
  const restoredTab = await promisedRestoredTab;
  is(restoredTab.index, 1, "Got valid index");

  await extension.unload();
  await BrowserTestUtils.closeWindow(win);
});
