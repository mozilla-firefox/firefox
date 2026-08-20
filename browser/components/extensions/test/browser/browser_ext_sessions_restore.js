"use strict";

SimpleTest.requestCompleteLog();

ChromeUtils.defineESModuleGetters(this, {
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  TabStateFlusher:
    "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs",
});

add_task(async function test_sessions_restore() {
  function background() {
    let notificationCount = 0;
    browser.sessions.onChanged.addListener(() => {
      notificationCount++;
      browser.test.sendMessage("notificationCount", notificationCount);
    });
    browser.test.onMessage.addListener((msg, data) => {
      if (msg == "check-sessions") {
        browser.sessions.getRecentlyClosed().then(recentlyClosed => {
          browser.test.sendMessage("recentlyClosed", recentlyClosed);
        });
      } else if (msg == "restore") {
        browser.sessions.restore(data).then(sessions => {
          browser.test.sendMessage("restored", sessions);
        });
      } else if (msg == "restore-reject") {
        browser.sessions.restore("not-a-valid-session-id").then(
          () => {
            browser.test.fail("restore rejected with an invalid sessionId");
          },
          error => {
            browser.test.assertTrue(
              error.message.includes(
                "Invalid sessionId: not-a-valid-session-id."
              )
            );
            browser.test.sendMessage("restore-rejected");
          }
        );
      }
    });
    browser.test.sendMessage("ready");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["sessions", "tabs"],
    },
    background,
  });

  async function assertNotificationCount(expected) {
    let notificationCount = await extension.awaitMessage("notificationCount");
    is(
      notificationCount,
      expected,
      "the expected number of notifications was fired"
    );
  }

  await extension.startup();

  const {
    Management: {
      global: { windowTracker, tabTracker },
    },
  } = ChromeUtils.importESModule("resource://gre/modules/Extension.sys.mjs");

  function checkLocalTab(tab, expectedUrl) {
    let realTab = tabTracker.getTab(tab.id);
    let tabState = JSON.parse(SessionStore.getTabState(realTab));
    is(
      tabState.entries[0].url,
      expectedUrl,
      "restored tab has the expected url"
    );
  }

  await extension.awaitMessage("ready");

  let win = await BrowserTestUtils.openNewBrowserWindow();
  BrowserTestUtils.startLoadingURIString(
    win.gBrowser.selectedBrowser,
    "about:config"
  );
  await BrowserTestUtils.browserLoaded(win.gBrowser.selectedBrowser);
  for (let url of ["about:robots", "about:mozilla"]) {
    await BrowserTestUtils.openNewForegroundTab(win.gBrowser, url);
  }
  await BrowserTestUtils.closeWindow(win);
  await assertNotificationCount(1);

  extension.sendMessage("check-sessions");
  let recentlyClosed = await extension.awaitMessage("recentlyClosed");

  // Check that our expected window is the most recently closed.
  is(
    recentlyClosed[0].window.tabs.length,
    3,
    "most recently closed window has the expected number of tabs"
  );

  // Restore the window.
  extension.sendMessage("restore");
  await assertNotificationCount(2);
  let restored = await extension.awaitMessage("restored");

  is(
    restored.window.tabs.length,
    3,
    "restore returned a window with the expected number of tabs"
  );
  checkLocalTab(restored.window.tabs[0], "about:config");
  checkLocalTab(restored.window.tabs[1], "about:robots");
  checkLocalTab(restored.window.tabs[2], "about:mozilla");

  // Close the window again.
  let window = windowTracker.getWindow(restored.window.id);
  await BrowserTestUtils.closeWindow(window);
  await assertNotificationCount(3);

  // Restore the window using the sessionId.
  extension.sendMessage("check-sessions");
  recentlyClosed = await extension.awaitMessage("recentlyClosed");
  extension.sendMessage("restore", recentlyClosed[0].window.sessionId);
  await assertNotificationCount(4);
  restored = await extension.awaitMessage("restored");

  is(
    restored.window.tabs.length,
    3,
    "restore returned a window with the expected number of tabs"
  );
  checkLocalTab(restored.window.tabs[0], "about:config");
  checkLocalTab(restored.window.tabs[1], "about:robots");
  checkLocalTab(restored.window.tabs[2], "about:mozilla");

  // Close the window again.
  window = windowTracker.getWindow(restored.window.id);
  await BrowserTestUtils.closeWindow(window);
  // notificationCount = yield extension.awaitMessage("notificationCount");
  await assertNotificationCount(5);

  // Open and close a tab.
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  await TabStateFlusher.flush(tab.linkedBrowser);
  BrowserTestUtils.removeTab(tab);
  await assertNotificationCount(6);

  // Restore the most recently closed item.
  extension.sendMessage("restore");
  await assertNotificationCount(7);
  restored = await extension.awaitMessage("restored");

  tab = restored.tab;
  ok(tab, "restore returned a tab");
  checkLocalTab(tab, "about:robots");

  // Close the tab again.
  let realTab = tabTracker.getTab(tab.id);
  BrowserTestUtils.removeTab(realTab);
  await assertNotificationCount(8);

  // Restore the tab using the sessionId.
  extension.sendMessage("check-sessions");
  recentlyClosed = await extension.awaitMessage("recentlyClosed");
  extension.sendMessage("restore", recentlyClosed[0].tab.sessionId);
  await assertNotificationCount(9);
  restored = await extension.awaitMessage("restored");

  tab = restored.tab;
  ok(tab, "restore returned a tab");
  checkLocalTab(tab, "about:robots");

  // Close the tab again.
  realTab = tabTracker.getTab(tab.id);
  BrowserTestUtils.removeTab(realTab);
  await assertNotificationCount(10);

  // Try to restore something with an invalid sessionId.
  extension.sendMessage("restore-reject");
  restored = await extension.awaitMessage("restore-rejected");

  await extension.unload();
});

add_task(async function test_sessions_event_page() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.eventPages.enabled", true]],
  });

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      browser_specific_settings: { gecko: { id: "eventpage@sessions" } },
      permissions: ["sessions", "tabs"],
      background: { persistent: false },
    },
    background() {
      browser.sessions.onChanged.addListener(() => {
        browser.test.sendMessage("changed");
      });
      browser.test.sendMessage("ready");
    },
  });

  await extension.startup();
  await extension.awaitMessage("ready");

  // test events waken background
  await extension.terminateBackground();
  let win = await BrowserTestUtils.openNewBrowserWindow();
  BrowserTestUtils.startLoadingURIString(
    win.gBrowser.selectedBrowser,
    "about:config"
  );
  await BrowserTestUtils.browserLoaded(win.gBrowser.selectedBrowser);
  for (let url of ["about:robots", "about:mozilla"]) {
    await BrowserTestUtils.openNewForegroundTab(win.gBrowser, url);
  }
  await BrowserTestUtils.closeWindow(win);

  await extension.awaitMessage("ready");
  await extension.awaitMessage("changed");
  ok(true, "persistent event woke background");

  await extension.unload();
  await SpecialPowers.popPrefEnv();
});
