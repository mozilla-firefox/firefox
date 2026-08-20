"use strict";

ChromeUtils.defineESModuleGetters(this, {
  SessionStore:
    "moz-src:///browser/components/sessionstore/SessionStore.sys.mjs",
  TabStateFlusher:
    "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs",
});

async function testTabsUpdateURL(
  existentTabURL,
  tabsUpdateURL,
  isErrorExpected
) {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    files: {
      "tab.html": `
        <!DOCTYPE html>
        <html>
          <head>
            <meta charset="utf-8">
          </head>
          <body>
            <h1>tab page</h1>
          </body>
        </html>
      `.trim(),
    },
    background: function () {
      browser.test.sendMessage("ready", browser.runtime.getURL("tab.html"));

      browser.test.onMessage.addListener(
        async (msg, tabsUpdateURL, isErrorExpected) => {
          let tabs = await browser.tabs.query({ lastFocusedWindow: true });

          try {
            let tab = await browser.tabs.update(tabs[1].id, {
              url: tabsUpdateURL,
            });

            browser.test.assertFalse(
              isErrorExpected,
              `tabs.update with URL ${tabsUpdateURL} should be rejected`
            );
            browser.test.assertTrue(
              tab,
              "on success the tab should be defined"
            );
          } catch (error) {
            browser.test.assertTrue(
              isErrorExpected,
              `tabs.update with URL ${tabsUpdateURL} should not be rejected`
            );
            browser.test.assertTrue(
              /^Illegal URL/.test(error.message),
              "tabs.update should be rejected with the expected error message"
            );
          }

          browser.test.sendMessage("done");
        }
      );
    },
  });

  await extension.startup();

  let mozExtTabURL = await extension.awaitMessage("ready");

  if (tabsUpdateURL == "self") {
    tabsUpdateURL = mozExtTabURL;
  }

  info(`tab.update URL "${tabsUpdateURL}" on tab with URL "${existentTabURL}"`);

  let tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    existentTabURL
  );

  extension.sendMessage("start", tabsUpdateURL, isErrorExpected);
  await extension.awaitMessage("done");

  BrowserTestUtils.removeTab(tab1);
  await extension.unload();
}

add_task(async function () {
  info("Start testing tabs.update on javascript URLs");

  let dataURLPage = `data:text/html,
    <!DOCTYPE html>
    <html>
      <head>
        <meta charset="utf-8">
      </head>
      <body>
        <h1>data url page</h1>
      </body>
    </html>`;

  let checkList = [
    {
      tabsUpdateURL: "http://example.net",
      isErrorExpected: false,
    },
    {
      tabsUpdateURL: "self",
      isErrorExpected: false,
    },
    {
      tabsUpdateURL: "about:addons",
      isErrorExpected: true,
    },
    {
      tabsUpdateURL: "javascript:console.log('tabs.update execute javascript')",
      isErrorExpected: true,
    },
    {
      tabsUpdateURL: dataURLPage,
      isErrorExpected: true,
    },
  ];

  let testCases = checkList.map(check =>
    Object.assign({}, check, { existentTabURL: "about:blank" })
  );

  for (let { existentTabURL, tabsUpdateURL, isErrorExpected } of testCases) {
    await testTabsUpdateURL(existentTabURL, tabsUpdateURL, isErrorExpected);
  }

  info("done");
});

add_task(async function test_update_reload() {
  const URL = "https://example.com/";

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background() {
      browser.test.onMessage.addListener(async (cmd, ...args) => {
        const result = await browser.tabs[cmd](...args);
        browser.test.sendMessage("result", result);
      });

      const filter = {
        properties: ["status"],
      };

      browser.tabs.onUpdated.addListener((tabId, changeInfo) => {
        if (changeInfo.status === "complete") {
          browser.test.sendMessage("historyAdded");
        }
      }, filter);
    },
  });

  let win = await BrowserTestUtils.openNewBrowserWindow();
  let tabBrowser = win.gBrowser.selectedBrowser;
  BrowserTestUtils.startLoadingURIString(tabBrowser, URL);
  await BrowserTestUtils.browserLoaded(tabBrowser, false, URL);
  let tab = win.gBrowser.selectedTab;

  async function getTabHistory() {
    await TabStateFlusher.flush(tabBrowser);
    return JSON.parse(SessionStore.getTabState(tab));
  }

  await extension.startup();
  extension.sendMessage("query", { url: URL });
  let tabs = await extension.awaitMessage("result");
  let tabId = tabs[0].id;

  let history = await getTabHistory();
  is(
    history.entries.length,
    1,
    "Tab history contains the expected number of entries."
  );
  is(
    history.entries[0].url,
    URL,
    `Tab history contains the expected entry: URL.`
  );

  extension.sendMessage("update", tabId, { url: `${URL}1/` });
  await Promise.all([
    extension.awaitMessage("result"),
    extension.awaitMessage("historyAdded"),
  ]);

  history = await getTabHistory();
  is(
    history.entries.length,
    2,
    "Tab history contains the expected number of entries."
  );
  is(
    history.entries[1].url,
    `${URL}1/`,
    `Tab history contains the expected entry: ${URL}1/.`
  );

  extension.sendMessage("update", tabId, {
    url: `${URL}2/`,
    loadReplace: true,
  });
  await Promise.all([
    extension.awaitMessage("result"),
    extension.awaitMessage("historyAdded"),
  ]);

  history = await getTabHistory();
  is(
    history.entries.length,
    2,
    "Tab history contains the expected number of entries."
  );
  is(
    history.entries[1].url,
    `${URL}2/`,
    `Tab history contains the expected entry: ${URL}2/.`
  );

  await extension.unload();
  await BrowserTestUtils.closeWindow(win);
});
