/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

add_task(async function tabsGroups_query() {
  async function loadExt(allowPrivate) {
    let ext = ExtensionTestUtils.loadExtension({
      manifest: {
        permissions: ["tabGroups"],
      },
      incognitoOverride: allowPrivate ? "spanning" : undefined,
      async background() {
        let [tab] = await browser.tabs.query({
          lastFocusedWindow: true,
          active: true,
        });
        browser.test.onMessage.addListener(async (_msg, tests) => {
          for (let { query, expected } of tests) {
            let groups = await browser.tabGroups.query(query);
            let titles = groups.map(group => group.title);
            browser.test.assertEq(
              expected.map(e => String(e)).join(),
              titles.sort().join(),
              `Expected groups for query - ${JSON.stringify(query)}`
            );
          }
          browser.test.sendMessage("done");
        });
        browser.test.sendMessage("windowId", tab.windowId);
      },
    });
    await ext.startup();
    return ext;
  }

  let ext = await loadExt();
  let windowId = await ext.awaitMessage("windowId");

  let url = "https://example.com/foo?";
  let groups = [];
  let tabs = [];
  let colors = ["red", "blue", "red", "pink", "green"];

  for (let i = 0; i < 5; i++) {
    tabs.push(await BrowserTestUtils.openNewForegroundTab(gBrowser, url + i));
    let group = gBrowser.addTabGroup(tabs.slice(-1));
    group.color = colors[i];
    group.name = String(i);
    groups.push(group);
  }
  groups[2].collapsed = true;
  groups[3].collapsed = true;

  let win2 = await BrowserTestUtils.openNewBrowserWindow();
  let tab5 = await BrowserTestUtils.openNewForegroundTab(win2.gBrowser, url);
  let tab6 = await BrowserTestUtils.openNewForegroundTab(win2.gBrowser, url);
  let group5 = win2.gBrowser.addTabGroup([tab5]);
  let group6 = win2.gBrowser.addTabGroup([tab6]);
  group5.name = "5";
  group6.name = "6";
  group5.color = "red";
  group6.color = "gray";
  group6.collapsed = true;

  ext.sendMessage("runTests", [
    {
      query: undefined,
      expected: [0, 1, 2, 3, 4, 5, 6],
    },
    {
      query: {},
      expected: [0, 1, 2, 3, 4, 5, 6],
    },
    {
      query: { collapsed: true },
      expected: [2, 3, 6],
    },
    {
      query: { collapsed: false },
      expected: [0, 1, 4, 5],
    },
    {
      query: { color: "red" },
      expected: [0, 2, 5],
    },
    {
      query: { color: "grey" },
      expected: [6],
    },
    {
      query: { title: "4" },
      expected: [4],
    },
    {
      query: { title: "*" },
      expected: [0, 1, 2, 3, 4, 5, 6],
    },
    {
      query: { windowId },
      expected: [0, 1, 2, 3, 4],
    },
    {
      query: { windowId: -2 },
      expected: [5, 6],
    },
    {
      query: { windowId: 1e9 },
      expected: [],
    },
    {
      query: { collapsed: true, color: "red" },
      expected: [2],
    },
    {
      query: { color: "red", windowId },
      expected: [0, 2],
    },
  ]);
  await ext.awaitMessage("done");

  groups[3].name = "Foo 3";
  group5.name = "FooBar 5";

  let win3 = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let tab7 = await BrowserTestUtils.openNewForegroundTab(win3.gBrowser, url);
  let group7 = win3.gBrowser.addTabGroup([tab7]);
  group7.name = "FooBaz 7";

  let ext2 = await loadExt(true);
  windowId = await ext2.awaitMessage("windowId");

  info("Extension without private browsing should't see group7 from win3.");
  ext.sendMessage("runTests", [
    {
      query: { title: "Foo*" },
      expected: ["Foo 3", "FooBar 5"],
    },
    {
      query: { windowId },
      expected: [],
    },
    {
      query: { windowId: -2 },
      expected: [],
    },
  ]);
  await ext.awaitMessage("done");

  info("Extension with private browsing access should see group7 from win3.");
  ext2.sendMessage("runTests", [
    {
      query: { title: "Foo*" },
      expected: ["Foo 3", "FooBar 5", "FooBaz 7"],
    },
    {
      query: { windowId },
      expected: ["FooBaz 7"],
    },
    {
      query: { windowId: -2 },
      expected: ["FooBaz 7"],
    },
  ]);
  await ext2.awaitMessage("done");

  for (let tab of tabs) {
    BrowserTestUtils.removeTab(tab);
  }
  await BrowserTestUtils.closeWindow(win2);
  await BrowserTestUtils.closeWindow(win3);

  await ext.unload();
  await ext2.unload();
});
