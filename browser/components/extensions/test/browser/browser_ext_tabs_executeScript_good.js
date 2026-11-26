"use strict";

requestLongerTimeout(2);

async function testHasPermission(params) {
  let contentSetup = params.contentSetup || (() => Promise.resolve());

  async function background(contentSetup) {
    browser.runtime.onMessage.addListener(msg => {
      browser.test.assertEq(msg, "script ran", "script ran");
      browser.test.notifyPass("executeScript");
    });

    browser.test.onMessage.addListener(msg => {
      browser.test.assertEq(msg, "execute-script");

      browser.tabs.executeScript({
        file: "script.js",
      });
    });

    await contentSetup();

    browser.test.sendMessage("ready");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: params.manifest,

    background: `(${background})(${contentSetup})`,

    files: {
      "panel.html": `<!DOCTYPE html>
        <html>
          <head><meta charset="utf-8"></head>
          <body>
          </body>
        </html>`,
      "script.js": function () {
        browser.runtime.sendMessage("script ran");
      },
    },
  });

  await extension.startup();
  await extension.awaitMessage("ready");

  if (params.setup) {
    await params.setup(extension);
  }

  extension.sendMessage("execute-script");

  await extension.awaitFinish("executeScript");

  if (params.tearDown) {
    await params.tearDown(extension);
  }

  await extension.unload();
}

add_task(async function testGoodPermissions() {
  let tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "http://mochi.test:8888/",
    true
  );

  info("Test activeTab permission with a command key press");
  await testHasPermission({
    manifest: {
      permissions: ["activeTab"],
      commands: {
        "test-tabs-executeScript": {
          suggested_key: {
            default: "Alt+Shift+K",
          },
        },
      },
    },
    contentSetup: function () {
      browser.commands.onCommand.addListener(function (command) {
        if (command == "test-tabs-executeScript") {
          browser.test.sendMessage("tabs-command-key-pressed");
        }
      });
      return Promise.resolve();
    },
    setup: async function (extension) {
      await EventUtils.synthesizeKey("k", { altKey: true, shiftKey: true });
      await extension.awaitMessage("tabs-command-key-pressed");
    },
  });

  info("Test activeTab permission with _execute_browser_action command");
  await testHasPermission({
    manifest: {
      permissions: ["activeTab"],
      browser_action: {},
      commands: {
        _execute_browser_action: {
          suggested_key: {
            default: "Alt+Shift+K",
          },
        },
      },
    },
    contentSetup: function () {
      browser.browserAction.onClicked.addListener(() => {
        browser.test.sendMessage("tabs-command-key-pressed");
      });
      return Promise.resolve();
    },
    setup: async function (extension) {
      await EventUtils.synthesizeKey("k", { altKey: true, shiftKey: true });
      await extension.awaitMessage("tabs-command-key-pressed");
    },
  });

  info("Test activeTab permission with _execute_page_action command");
  await testHasPermission({
    manifest: {
      permissions: ["activeTab"],
      page_action: {},
      commands: {
        _execute_page_action: {
          suggested_key: {
            default: "Alt+Shift+K",
          },
        },
      },
    },
    contentSetup: async function () {
      browser.pageAction.onClicked.addListener(() => {
        browser.test.sendMessage("tabs-command-key-pressed");
      });
      let [tab] = await browser.tabs.query({
        active: true,
        currentWindow: true,
      });
      await browser.pageAction.show(tab.id);
    },
    setup: async function (extension) {
      await EventUtils.synthesizeKey("k", { altKey: true, shiftKey: true });
      await extension.awaitMessage("tabs-command-key-pressed");
    },
  });

  info("Test activeTab permission with a context menu click");
  await testHasPermission({
    manifest: {
      permissions: ["activeTab", "contextMenus"],
    },
    contentSetup: function () {
      return new Promise(resolve => {
        browser.contextMenus.create(
          { title: "activeTab", contexts: ["all"] },
          resolve
        );
      });
    },
    setup: async function () {
      let contextMenu = document.getElementById("contentAreaContextMenu");
      let awaitPopupShown = BrowserTestUtils.waitForEvent(
        contextMenu,
        "popupshown"
      );
      let awaitPopupHidden = BrowserTestUtils.waitForEvent(
        contextMenu,
        "popuphidden"
      );

      await BrowserTestUtils.synthesizeMouseAtCenter(
        "a[href]",
        { type: "contextmenu", button: 2 },
        gBrowser.selectedBrowser
      );
      await awaitPopupShown;

      let item = contextMenu.querySelector("[label=activeTab]");

      contextMenu.activateItem(item);

      await awaitPopupHidden;
    },
  });

  BrowserTestUtils.removeTab(tab);
});

add_task(forceGC);
