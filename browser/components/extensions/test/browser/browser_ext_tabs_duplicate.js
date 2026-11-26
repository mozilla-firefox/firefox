"use strict";

add_task(async function testDuplicateTab() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "http://example.net/");

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background: async function () {
      let [source] = await browser.tabs.query({
        lastFocusedWindow: true,
        active: true,
      });

      let tab = await browser.tabs.duplicate(source.id);

      browser.test.assertEq(
        "http://example.net/",
        tab.url,
        "duplicated tab should have the same URL as the source tab"
      );
      browser.test.assertEq(
        source.index + 1,
        tab.index,
        "duplicated tab should open next to the source tab"
      );
      browser.test.assertTrue(
        tab.active,
        "duplicated tab should be active by default"
      );

      await browser.tabs.remove([source.id, tab.id]);
      browser.test.notifyPass("tabs.duplicate");
    },
  });

  await extension.startup();
  await extension.awaitFinish("tabs.duplicate");
  await extension.unload();
});

add_task(async function testDuplicateTabLazily() {
  async function background() {
    let tabLoadComplete = new Promise(resolve => {
      browser.test.onMessage.addListener((message, tabId) => {
        if (message == "duplicate-tab-done") {
          resolve(tabId);
        }
      });
    });

    function awaitLoad(url) {
      return new Promise(resolve => {
        browser.tabs.onUpdated.addListener(
          function listener(tabId, changed, tab) {
            if (changed.status == "complete" && tab.url == url) {
              browser.tabs.onUpdated.removeListener(listener);
              resolve();
            }
          }
        );
      });
    }

    try {
      let url =
        "http://example.com/browser/browser/components/extensions/test/browser/file_dummy.html";
      let loaded = awaitLoad(url);
      let tab = await browser.tabs.create({ url });
      let startTabId = tab.id;

      await loaded;
      browser.test.sendMessage("duplicate-tab", startTabId);

      let unloadedTabId = await tabLoadComplete;
      let loadedtab = await browser.tabs.get(startTabId);
      browser.test.assertEq(
        "Dummy test page",
        loadedtab.title,
        "Title should be returned for loaded pages"
      );
      browser.test.assertEq(
        "complete",
        loadedtab.status,
        "Tab status should be complete for loaded pages"
      );

      let unloadedtab = await browser.tabs.get(unloadedTabId);
      browser.test.assertEq(
        "Dummy test page",
        unloadedtab.title,
        "Title should be returned after page has been unloaded"
      );

      await browser.tabs.remove([tab.id, unloadedTabId]);
      browser.test.notifyPass("tabs.hasCorrectTabTitle");
    } catch (e) {
      browser.test.fail(`${e} :: ${e.stack}`);
      browser.test.notifyFail("tabs.hasCorrectTabTitle");
    }
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background,
  });

  extension.onMessage("duplicate-tab", tabId => {
    const {
      Management: {
        global: { tabTracker },
      },
    } = ChromeUtils.importESModule("resource://gre/modules/Extension.sys.mjs");

    let tab = tabTracker.getTab(tabId);
    // This is a bit of a hack to load a tab in the background.
    let newTab = gBrowser.duplicateTab(tab, true, { skipLoad: true });

    BrowserTestUtils.waitForEvent(newTab, "SSTabRestored", () => true).then(
      () => {
        extension.sendMessage("duplicate-tab-done", tabTracker.getId(newTab));
      }
    );
  });

  await extension.startup();
  await extension.awaitFinish("tabs.hasCorrectTabTitle");
  await extension.unload();
});

add_task(async function testDuplicatePinnedTab() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://example.net/"
  );
  gBrowser.pinTab(tab);

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background: async function () {
      let [source] = await browser.tabs.query({
        lastFocusedWindow: true,
        active: true,
      });
      let tab = await browser.tabs.duplicate(source.id);

      browser.test.assertEq(
        source.index + 1,
        tab.index,
        "duplicated tab should open next to the source tab"
      );
      browser.test.assertFalse(
        tab.pinned,
        "duplicated tab should not be pinned by default, even if source tab is"
      );

      await browser.tabs.remove([source.id, tab.id]);
      browser.test.notifyPass("tabs.duplicate.pinned");
    },
  });

  await extension.startup();
  await extension.awaitFinish("tabs.duplicate.pinned");
  await extension.unload();
});

add_task(async function testDuplicateTabInBackground() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "http://example.net/");

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background: async function () {
      let tabs = await browser.tabs.query({
        lastFocusedWindow: true,
        active: true,
      });
      let tab = await browser.tabs.duplicate(tabs[0].id, { active: false });
      // Should not be the active tab
      browser.test.assertFalse(tab.active);

      await browser.tabs.remove([tabs[0].id, tab.id]);
      browser.test.notifyPass("tabs.duplicate.background");
    },
  });

  await extension.startup();
  await extension.awaitFinish("tabs.duplicate.background");
  await extension.unload();
});

add_task(async function testDuplicateTabAtIndex() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "http://example.net/");

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background: async function () {
      let tabs = await browser.tabs.query({
        lastFocusedWindow: true,
        active: true,
      });
      let tab = await browser.tabs.duplicate(tabs[0].id, { index: 0 });
      browser.test.assertEq(0, tab.index);

      await browser.tabs.remove([tabs[0].id, tab.id]);
      browser.test.notifyPass("tabs.duplicate.index");
    },
  });

  await extension.startup();
  await extension.awaitFinish("tabs.duplicate.index");
  await extension.unload();
});

add_task(async function testDuplicatePinnedTabAtIncorrectIndex() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://example.net/"
  );
  gBrowser.pinTab(tab);

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background: async function () {
      let tabs = await browser.tabs.query({
        lastFocusedWindow: true,
        active: true,
      });
      let tab = await browser.tabs.duplicate(tabs[0].id, { index: 0 });
      browser.test.assertEq(1, tab.index);
      browser.test.assertFalse(
        tab.pinned,
        "Duplicated tab should not be pinned"
      );

      await browser.tabs.remove([tabs[0].id, tab.id]);
      browser.test.notifyPass("tabs.duplicate.incorrect_index");
    },
  });

  await extension.startup();
  await extension.awaitFinish("tabs.duplicate.incorrect_index");
  await extension.unload();
});

add_task(async function testDuplicateResolvePromiseRightAway() {
  await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://mochi.test:8888/browser/browser/components/extensions/test/browser/file_slowed_document.sjs"
  );

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      // The host permission matches the above URL. No :8888 due to bug 1468162.
      permissions: ["tabs", "http://mochi.test/"],
    },

    background: async function () {
      let [source] = await browser.tabs.query({
        lastFocusedWindow: true,
        active: true,
      });

      let resolvedRightAway = true;
      browser.tabs.onUpdated.addListener(
        () => {
          resolvedRightAway = false;
        },
        { urls: [source.url] }
      );

      let tab = await browser.tabs.duplicate(source.id);
      // if the promise is resolved before any onUpdated event has been fired,
      // then the promise has been resolved before waiting for the tab to load
      browser.test.assertTrue(
        resolvedRightAway,
        "tabs.duplicate() should resolve as soon as possible"
      );

      // Regression test for bug 1559216
      let code = "document.URL";
      let [result] = await browser.tabs.executeScript(tab.id, { code });
      browser.test.assertEq(
        source.url,
        result,
        "APIs such as tabs.executeScript should be queued until tabs.duplicate has restored the tab"
      );

      await browser.tabs.remove([source.id, tab.id]);
      browser.test.notifyPass("tabs.duplicate.resolvePromiseRightAway");
    },
  });

  await extension.startup();
  await extension.awaitFinish("tabs.duplicate.resolvePromiseRightAway");
  await extension.unload();
});
