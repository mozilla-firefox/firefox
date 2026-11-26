"use strict";

add_task(async function test_sessions_tab_value() {
  info("Testing set/get/deleteTabValue.");

  async function background() {
    let tests = [
      { key: "tabkey1", value: "Tab Value" },
      { key: "tabkey2", value: 25 },
      { key: "tabkey3", value: { val: "Tab Value" } },
      {
        key: "tabkey4",
        value: function () {
          return null;
        },
      },
    ];

    async function test(params) {
      let { key, value } = params;
      let tabs = await browser.tabs.query({
        currentWindow: true,
        active: true,
      });
      let currentTabId = tabs[0].id;

      browser.sessions.setTabValue(currentTabId, key, value);

      let testValue1 = await browser.sessions.getTabValue(currentTabId, key);
      let valueType = typeof value;

      browser.test.log(
        `Test that setting, getting and deleting tab value behaves properly when value is type "${valueType}"`
      );

      if (valueType == "string") {
        browser.test.assertEq(
          value,
          testValue1,
          `Value for key '${key}' should be '${value}'.`
        );
        browser.test.assertEq(
          "string",
          typeof testValue1,
          "typeof value should be '${valueType}'."
        );
      } else if (valueType == "number") {
        browser.test.assertEq(
          value,
          testValue1,
          `Value for key '${key}' should be '${value}'.`
        );
        browser.test.assertEq(
          "number",
          typeof testValue1,
          "typeof value should be '${valueType}'."
        );
      } else if (valueType == "object") {
        let innerVal1 = value.val;
        let innerVal2 = testValue1.val;
        browser.test.assertEq(
          innerVal1,
          innerVal2,
          `Value for key '${key}' should be '${innerVal1}'.`
        );
      } else if (valueType == "function") {
        browser.test.assertEq(
          null,
          testValue1,
          `Value for key '${key}' is non-JSON-able and should be 'null'.`
        );
      }

      // Remove the tab key/value.
      browser.sessions.removeTabValue(currentTabId, key);

      // This should now return undefined.
      testValue1 = await browser.sessions.getTabValue(currentTabId, key);
      browser.test.assertEq(
        undefined,
        testValue1,
        `Key has been deleted and value for key "${key}" should be 'undefined'.`
      );
    }

    for (let params of tests) {
      await test(params);
    }

    // Attempt to remove a non-existent key, should not throw error.
    let tabs = await browser.tabs.query({ currentWindow: true, active: true });
    await browser.sessions.removeTabValue(tabs[0].id, "non-existent-key");
    browser.test.succeed(
      "Attempting to remove a non-existent key should not fail."
    );

    browser.test.sendMessage("testComplete");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: "exampleextension@mozilla.org",
        },
      },
      permissions: ["sessions", "tabs"],
    },
    background,
  });

  await extension.startup();

  await extension.awaitMessage("testComplete");
  ok(true, "Testing completed for set/get/deleteTabValue.");

  await extension.unload();
});

add_task(async function test_sessions_tab_value_persistence() {
  info("Testing for persistence of set tab values.");

  async function background() {
    let key = "tabkey1";
    let value1 = "Tab Value 1a";
    let value2 = "Tab Value 1b";

    browser.test.log(
      "Test that two different tabs hold different values for a given key."
    );

    // Wait until the newly created tab has completed loading or it will still have
    // about:blank url when it gets removed and will not appear in the removed tabs history.
    browser.webNavigation.onCompleted.addListener(
      async function newTabListener() {
        browser.webNavigation.onCompleted.removeListener(newTabListener);

        let tabs = await browser.tabs.query({ currentWindow: true });

        let tabId_1 = tabs[0].id;
        let tabId_2 = tabs[1].id;

        browser.sessions.setTabValue(tabId_1, key, value1);
        browser.sessions.setTabValue(tabId_2, key, value2);

        let testValue1 = await browser.sessions.getTabValue(tabId_1, key);
        let testValue2 = await browser.sessions.getTabValue(tabId_2, key);

        browser.test.assertEq(
          value1,
          testValue1,
          `Value for key '${key}' should be '${value1}'.`
        );
        browser.test.assertEq(
          value2,
          testValue2,
          `Value for key '${key}' should be '${value2}'.`
        );

        browser.test.log(
          "Test that value is copied to duplicated tab for a given key."
        );

        let duptab = await browser.tabs.duplicate(tabId_2);
        let tabId_3 = duptab.id;

        let testValue3 = await browser.sessions.getTabValue(tabId_3, key);

        browser.test.assertEq(
          value2,
          testValue3,
          `Value for key '${key}' should be '${value2}'.`
        );

        browser.test.log(
          "Test that restored tab still holds the value for a given key."
        );

        await browser.tabs.remove([tabId_3]);

        let sessions = await browser.sessions.getRecentlyClosed({
          maxResults: 1,
        });

        let sessionData = await browser.sessions.restore(
          sessions[0].tab.sessionId
        );
        let restoredId = sessionData.tab.id;

        let testValue = await browser.sessions.getTabValue(restoredId, key);

        browser.test.assertEq(
          value2,
          testValue,
          `Value for key '${key}' should be '${value2}'.`
        );

        await browser.tabs.remove(tabId_2);
        await browser.tabs.remove(restoredId);

        browser.test.sendMessage("testComplete");
      },
      { url: [{ hostContains: "example.com" }] }
    );

    await browser.tabs.create({ url: "http://example.com" });
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: "exampleextension@mozilla.org",
        },
      },
      permissions: ["sessions", "tabs", "webNavigation"],
    },
    background,
  });

  await extension.startup();

  await extension.awaitMessage("testComplete");
  ok(true, "Testing completed for persistance of set tab values.");

  await extension.unload();
});

// Regression test for https://bugzilla.mozilla.org/show_bug.cgi?id=2002643
add_task(async function test_sessions_tab_value_persists_across_adoption() {
  async function background() {
    const tab = await browser.tabs.create({ url: "about:blank" });

    await browser.sessions.setTabValue(tab.id, "my key", "tab value");

    await browser.windows.create({ tabId: tab.id });

    browser.test.assertEq(
      await browser.sessions.getTabValue(tab.id, "my key"),
      "tab value",
      "setTabValue sticks to tab when tab is adopted in new window"
    );

    const win = await browser.windows.create({});
    await browser.tabs.group({
      createProperties: { windowId: win.id },
      tabIds: [tab.id],
    });

    browser.test.assertEq(
      await browser.sessions.getTabValue(tab.id, "my key"),
      "tab value",
      "setTabValue sticks to tab when tab is adopted in group in other window"
    );

    await browser.windows.remove(win.id);
    browser.test.sendMessage("testComplete");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: { permissions: ["sessions"] },
    background,
  });

  await extension.startup();
  await extension.awaitMessage("testComplete");
  await extension.unload();
});

add_task(async function test_sessions_window_value() {
  info("Testing set/get/deleteWindowValue.");

  async function background() {
    let tests = [
      { key: "winkey1", value: "Window Value" },
      { key: "winkey2", value: 25 },
      { key: "winkey3", value: { val: "Window Value" } },
      {
        key: "winkey4",
        value: function () {
          return null;
        },
      },
    ];

    async function test(params) {
      let { key, value } = params;
      let win = await browser.windows.getCurrent();
      let currentWinId = win.id;

      browser.sessions.setWindowValue(currentWinId, key, value);

      let testValue1 = await browser.sessions.getWindowValue(currentWinId, key);
      let valueType = typeof value;

      browser.test.log(
        `Test that setting, getting and deleting window value behaves properly when value is type "${valueType}"`
      );

      if (valueType == "string") {
        browser.test.assertEq(
          value,
          testValue1,
          `Value for key '${key}' should be '${value}'.`
        );
        browser.test.assertEq(
          "string",
          typeof testValue1,
          "typeof value should be '${valueType}'."
        );
      } else if (valueType == "number") {
        browser.test.assertEq(
          value,
          testValue1,
          `Value for key '${key}' should be '${value}'.`
        );
        browser.test.assertEq(
          "number",
          typeof testValue1,
          "typeof value should be '${valueType}'."
        );
      } else if (valueType == "object") {
        let innerVal1 = value.val;
        let innerVal2 = testValue1.val;
        browser.test.assertEq(
          innerVal1,
          innerVal2,
          `Value for key '${key}' should be '${innerVal1}'.`
        );
      } else if (valueType == "function") {
        browser.test.assertEq(
          null,
          testValue1,
          `Value for key '${key}' is non-JSON-able and should be 'null'.`
        );
      }

      // Remove the window key/value.
      browser.sessions.removeWindowValue(currentWinId, key);

      // This should return undefined as the key no longer exists.
      testValue1 = await browser.sessions.getWindowValue(currentWinId, key);
      browser.test.assertEq(
        undefined,
        testValue1,
        `Key has been deleted and value for key '${key}' should be 'undefined'.`
      );
    }

    for (let params of tests) {
      await test(params);
    }

    // Attempt to remove a non-existent key, should not throw error.
    let win = await browser.windows.getCurrent();
    await browser.sessions.removeWindowValue(win.id, "non-existent-key");
    browser.test.succeed(
      "Attempting to remove a non-existent key should not fail."
    );

    browser.test.sendMessage("testComplete");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: "exampleextension@mozilla.org",
        },
      },
      permissions: ["sessions"],
    },
    background,
  });

  await extension.startup();

  await extension.awaitMessage("testComplete");
  ok(true, "Testing completed for set/get/deleteWindowValue.");

  await extension.unload();
});

add_task(async function test_sessions_window_value_persistence() {
  info(
    "Testing that different values for the same key in different windows are persisted properly."
  );

  async function background() {
    let key = "winkey1";
    let value1 = "Window Value 1a";
    let value2 = "Window Value 1b";

    let window1 = await browser.windows.getCurrent();
    let window2 = await browser.windows.create({});

    let window1Id = window1.id;
    let window2Id = window2.id;

    browser.sessions.setWindowValue(window1Id, key, value1);
    browser.sessions.setWindowValue(window2Id, key, value2);

    let testValue1 = await browser.sessions.getWindowValue(window1Id, key);
    let testValue2 = await browser.sessions.getWindowValue(window2Id, key);

    browser.test.assertEq(
      value1,
      testValue1,
      `Value for key '${key}' should be '${value1}'.`
    );
    browser.test.assertEq(
      value2,
      testValue2,
      `Value for key '${key}' should be '${value2}'.`
    );

    await browser.windows.remove(window2Id);
    browser.test.sendMessage("testComplete");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      browser_specific_settings: {
        gecko: {
          id: "exampleextension@mozilla.org",
        },
      },
      permissions: ["sessions"],
    },
    background,
  });

  await extension.startup();

  await extension.awaitMessage("testComplete");
  ok(true, "Testing completed for persistance of set window values.");

  await extension.unload();
});
