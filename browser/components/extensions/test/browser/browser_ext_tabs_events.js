"use strict";

// TODO(Bug 1938594): Remove this allowlisted uncaught rejection once we clarified how
// the underlying issue hit by SessionStore.onMoveToNewWindow will be
// fixed.
PromiseTestUtils.allowMatchingRejectionsGlobally(
  /Unexpected undefined tabState for onMoveToNewWindow/
);

// A single monitor for the tests.  If it receives any
// incognito data in event listeners it will fail.
let monitor;
add_task(async function startup() {
  monitor = await startIncognitoMonitorExtension();
});
registerCleanupFunction(async function finish() {
  await monitor.unload();
});

// Test tab events from private windows, the monitor above will fail
// if it receives any.
add_task(async function test_tab_events_incognito_monitored() {
  async function background() {
    let incognito = true;
    let events = [];
    let eventPromise;
    let checkEvents = () => {
      if (eventPromise && events.length >= eventPromise.names.length) {
        eventPromise.resolve();
      }
    };

    browser.tabs.onCreated.addListener(tab => {
      events.push({ type: "onCreated", tab });
      checkEvents();
    });

    browser.tabs.onAttached.addListener((tabId, info) => {
      events.push(Object.assign({ type: "onAttached", tabId }, info));
      checkEvents();
    });

    browser.tabs.onDetached.addListener((tabId, info) => {
      events.push(Object.assign({ type: "onDetached", tabId }, info));
      checkEvents();
    });

    browser.tabs.onRemoved.addListener((tabId, info) => {
      events.push(Object.assign({ type: "onRemoved", tabId }, info));
      checkEvents();
    });

    browser.tabs.onMoved.addListener((tabId, info) => {
      events.push(Object.assign({ type: "onMoved", tabId }, info));
      checkEvents();
    });

    async function expectEvents(names) {
      browser.test.log(`Expecting events: ${names.join(", ")}`);

      await new Promise(resolve => {
        eventPromise = { names, resolve };
        checkEvents();
      });

      browser.test.assertEq(
        names.length,
        events.length,
        "Got expected number of events"
      );
      for (let [i, name] of names.entries()) {
        browser.test.assertEq(
          name,
          i in events && events[i].type,
          `Got expected ${name} event`
        );
      }
      return events.splice(0);
    }

    try {
      let firstWindow = await browser.windows.create({
        url: "about:blank",
        incognito,
      });
      let otherWindow = await browser.windows.create({
        url: "about:blank",
        incognito,
      });

      let windowId = firstWindow.id;
      let otherWindowId = otherWindow.id;

      // Wait for a tab in each window
      await expectEvents(["onCreated", "onCreated"]);
      let initialTab = (
        await browser.tabs.query({
          active: true,
          windowId: otherWindowId,
        })
      )[0];

      browser.test.log("Create tab in window 1");
      let tab = await browser.tabs.create({
        windowId,
        index: 0,
        url: "about:blank",
      });
      let oldIndex = tab.index;
      browser.test.assertEq(0, oldIndex, "Tab has the expected index");
      browser.test.assertEq(tab.incognito, incognito, "Tab is incognito");

      let [created] = await expectEvents(["onCreated"]);
      browser.test.assertEq(tab.id, created.tab.id, "Got expected tab ID");
      browser.test.assertEq(
        oldIndex,
        created.tab.index,
        "Got expected tab index"
      );

      browser.test.log("Move tab to window 2");
      await browser.tabs.move([tab.id], { windowId: otherWindowId, index: 0 });

      let [detached, attached] = await expectEvents([
        "onDetached",
        "onAttached",
      ]);
      browser.test.assertEq(
        tab.id,
        detached.tabId,
        "Expected onDetached tab ID"
      );
      browser.test.assertEq(
        oldIndex,
        detached.oldPosition,
        "Expected old index"
      );
      browser.test.assertEq(
        windowId,
        detached.oldWindowId,
        "Expected old window ID"
      );

      browser.test.assertEq(
        tab.id,
        attached.tabId,
        "Expected onAttached tab ID"
      );
      browser.test.assertEq(0, attached.newPosition, "Expected new index");
      browser.test.assertEq(
        otherWindowId,
        attached.newWindowId,
        "Expected new window ID"
      );

      browser.test.log("Move tab within the same window");
      let [moved] = await browser.tabs.move([tab.id], { index: 1 });
      browser.test.assertEq(1, moved.index, "Expected new index");

      [moved] = await expectEvents(["onMoved"]);
      browser.test.assertEq(tab.id, moved.tabId, "Expected tab ID");
      browser.test.assertEq(0, moved.fromIndex, "Expected old index");
      browser.test.assertEq(1, moved.toIndex, "Expected new index");
      browser.test.assertEq(
        otherWindowId,
        moved.windowId,
        "Expected window ID"
      );

      browser.test.log("Remove tab");
      await browser.tabs.remove(tab.id);
      let [removed] = await expectEvents(["onRemoved"]);

      browser.test.assertEq(
        tab.id,
        removed.tabId,
        "Expected removed tab ID for tabs.remove"
      );
      browser.test.assertEq(
        otherWindowId,
        removed.windowId,
        "Expected removed tab window ID"
      );
      // Note: We want to test for the actual boolean value false here.
      browser.test.assertEq(
        false,
        removed.isWindowClosing,
        "Expected isWindowClosing value"
      );

      browser.test.log("Close second window");
      await browser.windows.remove(otherWindowId);
      [removed] = await expectEvents(["onRemoved"]);
      browser.test.assertEq(
        initialTab.id,
        removed.tabId,
        "Expected removed tab ID for windows.remove"
      );
      browser.test.assertEq(
        otherWindowId,
        removed.windowId,
        "Expected removed tab window ID"
      );
      browser.test.assertEq(
        true,
        removed.isWindowClosing,
        "Expected isWindowClosing value"
      );

      browser.test.log("Create additional tab in window 1");
      tab = await browser.tabs.create({ windowId, url: "about:blank" });
      await expectEvents(["onCreated"]);
      browser.test.assertEq(tab.incognito, incognito, "Tab is incognito");

      browser.test.log("Create a new window, adopting the new tab");
      // We have to explicitly wait for the event here, since its timing is
      // not predictable.
      let promiseAttached = new Promise(resolve => {
        browser.tabs.onAttached.addListener(function listener() {
          browser.tabs.onAttached.removeListener(listener);
          resolve();
        });
      });

      let [window] = await Promise.all([
        browser.windows.create({ tabId: tab.id, incognito }),
        promiseAttached,
      ]);

      [detached, attached] = await expectEvents(["onDetached", "onAttached"]);

      browser.test.assertEq(
        tab.id,
        detached.tabId,
        "Expected onDetached tab ID"
      );
      browser.test.assertEq(
        1,
        detached.oldPosition,
        "Expected onDetached old index"
      );
      browser.test.assertEq(
        windowId,
        detached.oldWindowId,
        "Expected onDetached old window ID"
      );

      browser.test.assertEq(
        tab.id,
        attached.tabId,
        "Expected onAttached tab ID"
      );
      browser.test.assertEq(
        0,
        attached.newPosition,
        "Expected onAttached new index"
      );
      browser.test.assertEq(
        window.id,
        attached.newWindowId,
        "Expected onAttached new window id"
      );

      browser.test.log(
        "Close the new window by moving the tab into former window"
      );
      await browser.tabs.move(tab.id, { index: 1, windowId });
      [detached, attached] = await expectEvents(["onDetached", "onAttached"]);

      browser.test.assertEq(
        tab.id,
        detached.tabId,
        "Expected onDetached tab ID"
      );
      browser.test.assertEq(
        0,
        detached.oldPosition,
        "Expected onDetached old index"
      );
      browser.test.assertEq(
        window.id,
        detached.oldWindowId,
        "Expected onDetached old window ID"
      );

      browser.test.assertEq(
        tab.id,
        attached.tabId,
        "Expected onAttached tab ID"
      );
      browser.test.assertEq(
        1,
        attached.newPosition,
        "Expected onAttached new index"
      );
      browser.test.assertEq(
        windowId,
        attached.newWindowId,
        "Expected onAttached new window id"
      );
      browser.test.assertEq(tab.incognito, incognito, "Tab is incognito");

      browser.test.log("Remove the tab");
      await browser.tabs.remove(tab.id);
      browser.windows.remove(windowId);

      browser.test.notifyPass("tabs-events");
    } catch (e) {
      browser.test.fail(`${e} :: ${e.stack}`);
      browser.test.notifyFail("tabs-events");
    }
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },
    background,
    incognitoOverride: "spanning",
  });

  await extension.startup();
  await extension.awaitFinish("tabs-events");
  await extension.unload();
});

add_task(async function testTabEventsSize() {
  function background() {
    function sendSizeMessages(tab, type) {
      browser.test.sendMessage(`${type}-dims`, {
        width: tab.width,
        height: tab.height,
      });
    }

    browser.tabs.onCreated.addListener(tab => {
      sendSizeMessages(tab, "on-created");
    });

    browser.tabs.onUpdated.addListener((tabId, changeInfo, tab) => {
      if (changeInfo.status == "complete") {
        sendSizeMessages(tab, "on-updated");
      }
    });

    browser.test.onMessage.addListener(async (msg, arg) => {
      if (msg === "create-tab") {
        let tab = await browser.tabs.create({ url: "https://example.com/" });
        sendSizeMessages(tab, "create");
        browser.test.sendMessage("created-tab-id", tab.id);
      } else if (msg === "update-tab") {
        let tab = await browser.tabs.update(arg, {
          url: "https://example.org/",
        });
        sendSizeMessages(tab, "update");
      } else if (msg === "remove-tab") {
        browser.tabs.remove(arg);
        browser.test.sendMessage("tab-removed");
      }
    });

    browser.test.sendMessage("ready");
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },
    background,
  });

  const RESOLUTION_PREF = "layout.css.devPixelsPerPx";
  registerCleanupFunction(() => {
    SpecialPowers.clearUserPref(RESOLUTION_PREF);
  });

  function checkDimensions(dims, type) {
    is(
      dims.width,
      gBrowser.selectedBrowser.clientWidth,
      `tab from ${type} reports expected width`
    );
    is(
      dims.height,
      gBrowser.selectedBrowser.clientHeight,
      `tab from ${type} reports expected height`
    );
  }

  await Promise.all([extension.startup(), extension.awaitMessage("ready")]);

  for (let resolution of [2, 1]) {
    SpecialPowers.setCharPref(RESOLUTION_PREF, String(resolution));
    is(
      window.devicePixelRatio,
      resolution,
      "window has the required resolution"
    );

    extension.sendMessage("create-tab");
    let tabId = await extension.awaitMessage("created-tab-id");

    checkDimensions(await extension.awaitMessage("create-dims"), "create");
    checkDimensions(
      await extension.awaitMessage("on-created-dims"),
      "onCreated"
    );
    checkDimensions(
      await extension.awaitMessage("on-updated-dims"),
      "onUpdated"
    );

    extension.sendMessage("update-tab", tabId);

    checkDimensions(await extension.awaitMessage("update-dims"), "update");
    checkDimensions(
      await extension.awaitMessage("on-updated-dims"),
      "onUpdated"
    );

    extension.sendMessage("remove-tab", tabId);
    await extension.awaitMessage("tab-removed");
  }

  await extension.unload();
  SpecialPowers.clearUserPref(RESOLUTION_PREF);
}).skip(); // Bug 1614075 perma-fail comparing devicePixelRatio

add_task(async function testTabRemovalEvent() {
  async function background() {
    let events = [];

    function awaitLoad(tabId) {
      return new Promise(resolve => {
        browser.tabs.onUpdated.addListener(function listener(tabId_, changed) {
          if (tabId == tabId_ && changed.status == "complete") {
            browser.tabs.onUpdated.removeListener(listener);
            resolve();
          }
        });
      });
    }

    chrome.tabs.onRemoved.addListener(tabId => {
      browser.test.assertEq(
        0,
        events.length,
        "No events recorded before onRemoved."
      );
      events.push("onRemoved");
      browser.test.log(
        "Make sure the removed tab is not available in the tabs.query callback."
      );
      chrome.tabs.query({}, tabs => {
        for (let tab of tabs) {
          browser.test.assertTrue(
            tab.id != tabId,
            "Tab query should not include removed tabId"
          );
        }
      });
    });

    try {
      let url =
        "https://example.com/browser/browser/components/extensions/test/browser/context.html";
      let tab = await browser.tabs.create({ url: url });
      await awaitLoad(tab.id);

      chrome.tabs.onActivated.addListener(() => {
        browser.test.assertEq(
          1,
          events.length,
          "One event recorded before onActivated."
        );
        events.push("onActivated");
        browser.test.assertEq(
          "onRemoved",
          events[0],
          "onRemoved fired before onActivated."
        );
        browser.test.notifyPass("tabs-events");
      });

      await browser.tabs.remove(tab.id);
    } catch (e) {
      browser.test.fail(`${e} :: ${e.stack}`);
      browser.test.notifyFail("tabs-events");
    }
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background,
  });

  await extension.startup();
  await extension.awaitFinish("tabs-events");
  await extension.unload();
});

add_task(async function testTabCreateRelated() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.tabs.opentabfor.middleclick", true],
      ["browser.tabs.insertRelatedAfterCurrent", true],
    ],
  });

  async function background() {
    let created;
    browser.tabs.onCreated.addListener(tab => {
      browser.test.log(`tabs.onCreated, index=${tab.index}`);
      browser.test.assertEq(1, tab.index, "expecting tab index of 1");
      created = tab.id;
    });
    browser.tabs.onMoved.addListener((id, info) => {
      browser.test.log(
        `tabs.onMoved, from ${info.fromIndex} to ${info.toIndex}`
      );
      browser.test.fail("tabMoved was received");
    });
    browser.tabs.onRemoved.addListener(tabId => {
      browser.test.assertEq(created, tabId, "removed id same as created");
      browser.test.sendMessage("tabRemoved");
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },

    background,
  });

  // Create a *opener* tab page which has a link to "example.com".
  let pageURL =
    "https://example.com/browser/browser/components/extensions/test/browser/file_dummy.html";
  let openerTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    pageURL
  );
  gBrowser.moveTabToStart(openerTab);

  await extension.startup();

  let newTabPromise = BrowserTestUtils.waitForNewTab(
    gBrowser,
    "https://example.com/#linkclick",
    true
  );
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "#link_to_example_com",
    { button: 1 },
    gBrowser.selectedBrowser
  );
  let openTab = await newTabPromise;
  is(
    openTab.linkedBrowser.currentURI.spec,
    "https://example.com/#linkclick",
    "Middle click should open site to correct url."
  );
  BrowserTestUtils.removeTab(openTab);

  await extension.awaitMessage("tabRemoved");
  await extension.unload();

  BrowserTestUtils.removeTab(openerTab);
});

add_task(async function testLastTabRemoval() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.tabs.closeWindowWithLastTab", false]],
  });

  async function background() {
    let windowId;
    browser.tabs.onCreated.addListener(tab => {
      browser.test.assertEq(
        windowId,
        tab.windowId,
        "expecting onCreated after onRemoved on the same window"
      );
      browser.test.sendMessage("tabCreated", `${tab.width}x${tab.height}`);
    });
    browser.tabs.onRemoved.addListener((tabId, info) => {
      windowId = info.windowId;
    });
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },
    background,
  });

  let newWin = await BrowserTestUtils.openNewBrowserWindow();
  await extension.startup();

  const oldBrowser = newWin.gBrowser.selectedBrowser;
  const expectedDims = `${oldBrowser.clientWidth}x${oldBrowser.clientHeight}`;
  BrowserTestUtils.removeTab(newWin.gBrowser.selectedTab);

  const actualDims = await extension.awaitMessage("tabCreated");
  is(
    actualDims,
    expectedDims,
    "created tab reports a size same to the removed last tab"
  );

  await extension.unload();
  await BrowserTestUtils.closeWindow(newWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function testTabActivationEvent() {
  async function background() {
    function makeExpectable() {
      let expectation = null,
        resolver = null;
      const expectable = param => {
        if (expectation === null) {
          browser.test.fail("unexpected call to expectable");
        } else {
          try {
            resolver(expectation(param));
          } catch (e) {
            resolver(Promise.reject(e));
          } finally {
            expectation = null;
          }
        }
      };
      expectable.expect = e => {
        expectation = e;
        return new Promise(r => {
          resolver = r;
        });
      };
      return expectable;
    }
    try {
      const listener = makeExpectable();
      browser.tabs.onActivated.addListener(listener);

      const [
        ,
        {
          tabs: [tab1],
        },
      ] = await Promise.all([
        listener.expect(info => {
          browser.test.assertEq(
            undefined,
            info.previousTabId,
            "previousTabId should not be defined when window is first opened"
          );
        }),
        browser.windows.create({ url: "about:blank" }),
      ]);
      const [, tab2] = await Promise.all([
        listener.expect(info => {
          browser.test.assertEq(
            tab1.id,
            info.previousTabId,
            "Got expected previousTabId"
          );
        }),
        browser.tabs.create({ url: "about:blank" }),
      ]);

      await Promise.all([
        listener.expect(info => {
          browser.test.assertEq(tab1.id, info.tabId, "Got expected tabId");
          browser.test.assertEq(
            tab2.id,
            info.previousTabId,
            "Got expected previousTabId"
          );
        }),
        browser.tabs.update(tab1.id, { active: true }),
      ]);

      await Promise.all([
        listener.expect(info => {
          browser.test.assertEq(tab2.id, info.tabId, "Got expected tabId");
          browser.test.assertEq(
            undefined,
            info.previousTabId,
            "previousTabId should not be defined when previous tab was closed"
          );
        }),
        browser.tabs.remove(tab1.id),
      ]);

      await browser.tabs.remove(tab2.id);

      browser.test.notifyPass("tabs-events");
    } catch (e) {
      browser.test.fail(`${e} :: ${e.stack}`);
      browser.test.notifyFail("tabs-events");
    }
  }

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabs"],
    },
    background,
  });

  await extension.startup();
  await extension.awaitFinish("tabs-events");
  await extension.unload();
});

add_task(async function test_tabs_event_page() {
  await SpecialPowers.pushPrefEnv({
    set: [["extensions.eventPages.enabled", true]],
  });

  let extension = ExtensionTestUtils.loadExtension({
    useAddonManager: "permanent",
    manifest: {
      browser_specific_settings: { gecko: { id: "eventpage@tabs" } },
      permissions: ["tabs"],
      background: { persistent: false },
    },
    background() {
      const EVENTS = [
        "onActivated",
        "onAttached",
        "onDetached",
        "onRemoved",
        "onMoved",
        "onHighlighted",
        "onUpdated",
      ];
      browser.tabs.onCreated.addListener(() => {
        browser.test.sendMessage("onCreated");
      });
      for (let event of EVENTS) {
        browser.tabs[event].addListener(() => {});
      }
      browser.test.sendMessage("ready");
    },
  });

  const EVENTS = [
    "onActivated",
    "onAttached",
    "onCreated",
    "onDetached",
    "onRemoved",
    "onMoved",
    "onHighlighted",
    "onUpdated",
  ];

  await extension.startup();
  await extension.awaitMessage("ready");
  for (let event of EVENTS) {
    assertPersistentListeners(extension, "tabs", event, {
      primed: false,
    });
  }

  // test events waken background
  await extension.terminateBackground();
  for (let event of EVENTS) {
    assertPersistentListeners(extension, "tabs", event, {
      primed: true,
    });
  }

  let win = await BrowserTestUtils.openNewBrowserWindow();

  await extension.awaitMessage("ready");
  await extension.awaitMessage("onCreated");
  ok(true, "persistent event woke background");
  for (let event of EVENTS) {
    assertPersistentListeners(extension, "tabs", event, {
      primed: false,
    });
  }
  await BrowserTestUtils.closeWindow(win);

  await extension.unload();
  await SpecialPowers.popPrefEnv();
});
