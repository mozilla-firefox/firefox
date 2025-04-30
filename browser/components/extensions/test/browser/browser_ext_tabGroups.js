/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

const {
  Management: {
    global: { getExtTabGroupIdForInternalTabGroupId },
  },
} = ChromeUtils.importESModule("resource://gre/modules/Extension.sys.mjs");

add_task(async function tabsGroups_get_private() {
  async function loadExt(allowPrivate) {
    let ext = ExtensionTestUtils.loadExtension({
      manifest: {
        permissions: ["tabGroups"],
      },
      incognitoOverride: allowPrivate ? "spanning" : undefined,
      background() {
        browser.test.onMessage.addListener(async groupId => {
          try {
            let group = await browser.tabGroups.get(groupId);

            let color = browser.tabGroups.Color[group.color.toUpperCase()];
            browser.test.assertEq(group.color, color, "A known colour.");

            browser.test.sendMessage("group", group);
          } catch (e) {
            browser.test.sendMessage("error", e.message);
          }
        });
      },
    });
    await ext.startup();
    return ext;
  }

  let url = "https://example.com/?";
  let tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);
  let group1 = gBrowser.addTabGroup([tab1]);
  let gid1 = getExtTabGroupIdForInternalTabGroupId(group1.id);

  let win2 = await BrowserTestUtils.openNewBrowserWindow({ private: true });
  let tab2 = await BrowserTestUtils.openNewForegroundTab(win2.gBrowser, url);
  let group2 = win2.gBrowser.addTabGroup([tab2]);
  let gid2 = getExtTabGroupIdForInternalTabGroupId(group2.id);

  function compare(group, ext, info) {
    is(group.collapsed, ext.collapsed, `Collapsed ok - ${info}`);
    is(group.name, ext.title, `Title ok - ${info}`);
    is(group.color.replace(/^gray$/, "grey"), ext.color, `Color ok - ${info}`);
  }

  info("Testing extension without private browsing access.");
  let ext1 = await loadExt(false);

  ext1.sendMessage(gid1);
  let g1 = await ext1.awaitMessage("group");
  compare(group1, g1, "Ext 1 / group 1.");

  ext1.sendMessage(gid2);
  let e2 = await ext1.awaitMessage("error");
  is(e2, `No group with id: ${gid2}`, "Expected error.");
  await ext1.unload();

  info("Testing extension with private browsing access.");
  let ext2 = await loadExt(true);

  ext2.sendMessage(gid1);
  g1 = await ext2.awaitMessage("group");
  compare(group1, g1, "Ext 2 / group 1.");

  ext2.sendMessage(gid1);
  let g2 = await ext2.awaitMessage("group");
  compare(group1, g2, "Ext 2 / group 2.");

  await ext2.unload();
  BrowserTestUtils.removeTab(tab1);
  await BrowserTestUtils.closeWindow(win2);
});

add_task(async function tabsGroups_update_onUpdated() {
  let url = "https://example.com/?";
  let tab1 = await BrowserTestUtils.openNewForegroundTab(gBrowser, url + 1);
  let tab2 = await BrowserTestUtils.openNewForegroundTab(gBrowser, url + 2);

  let group1 = gBrowser.addTabGroup([tab1]);
  let group2 = gBrowser.addTabGroup([tab2]);
  gBrowser.selectedTab = tab1;

  info("Setup initial group colour for consistency.");
  group1.color = "red";
  group2.color = "blue";

  let ext = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabGroups"],
    },
    async background() {
      let [tab] = await browser.tabs.query({
        lastFocusedWindow: true,
        active: true,
      });

      let group = await browser.tabGroups.get(tab.groupId);
      browser.test.assertEq(tab.groupId, group.id, "Group id matches.");
      browser.test.assertEq(tab.windowId, group.windowId, "Window id ok.");

      browser.tabGroups.onCreated.addListener(group => {
        browser.test.sendMessage("created", group);
      });
      browser.tabGroups.onRemoved.addListener(group => {
        browser.test.sendMessage("removed", group);
      });

      browser.tabGroups.onUpdated.addListener(async updated => {
        let group = await browser.tabGroups.get(updated.id);
        browser.test.assertDeepEq(updated, group, "It's the same group.");
        browser.test.sendMessage("updated", updated);
      });

      browser.test.assertThrows(
        () => browser.tabGroups.update(1e9, { color: "magenta" }),
        /Invalid enumeration value "magenta"/,
        "Magenta is not a real color."
      );

      await browser.test.assertRejects(
        browser.tabGroups.update(1e9, { title: "blah" }),
        "No group with id: 1000000000",
        "Invalid group id rejects."
      );

      browser.test.onMessage.addListener((_msg, update) => {
        browser.tabGroups.update(group.id, update);
      });
      browser.test.sendMessage("group", group);
    },
  });
  await ext.startup();

  function compare(first, second, info) {
    let { color, title, collapsed } = first;

    // XUL element has a .name, and uses "gray".
    if (XULElement.isInstance(first)) {
      color = first.color.replace(/^gray$/, "grey");
      title = first.name;
    }

    is(collapsed, second.collapsed, `Collapsed ok - ${info}`);
    is(color, second.color, `Color ok - ${info}`);
    is(title, second.title, `Title ok - ${info}`);
  }

  let first = await ext.awaitMessage("group");
  compare(group1, first, "Initial group.");

  info("Updating group1 using extension api.");
  ext.sendMessage("update", { collapsed: true });
  first.collapsed = true;

  let updated = await ext.awaitMessage("updated");
  compare(first, updated, "After collapsing 1.");

  ext.sendMessage("update", { collapsed: false });
  first.collapsed = false;

  updated = await ext.awaitMessage("updated");
  compare(first, updated, "After expanding 1.");

  ext.sendMessage("update", { color: "grey" });
  first.color = "grey";

  updated = await ext.awaitMessage("updated");
  compare(first, updated, "After coloring 1.");

  ext.sendMessage("update", { title: "First" });
  first.title = "First";

  updated = await ext.awaitMessage("updated");
  compare(first, updated, "After naming 1.");

  info("Creating a new group does not trigger onUpdated event.");
  let tab3 = await BrowserTestUtils.openNewForegroundTab(gBrowser, url + 3);
  gBrowser.addTabGroup([tab3]);
  await ext.awaitMessage("created");

  info("Closing tab1, and thus its group, does not trigger onUpdated.");
  BrowserTestUtils.removeTab(tab1);
  await ext.awaitMessage("removed");

  info("Updating group2 directly from outside of the extension.");
  group2.collapsed = true;
  let second = await ext.awaitMessage("updated");
  compare(group2, second, "After collapsing 2.");

  group2.color = "pink";
  second = await ext.awaitMessage("updated");
  compare(group2, second, "After coloring 2.");

  group2.name = "Second";
  second = await ext.awaitMessage("updated");
  compare(group2, second, "After naming 2.");

  isnot(first.id, second.id, "Groups have different ids.");
  is(first.windowId, second.windowId, "Groups in the same window.");

  await ext.unload();
  BrowserTestUtils.removeTab(tab3);
  BrowserTestUtils.removeTab(tab2);
});
