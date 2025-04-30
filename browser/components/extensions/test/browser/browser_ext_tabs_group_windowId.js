/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

// TODO bug 1938594: group_across_private_browsing_windows sometimes triggers
// this error. See https://bugzilla.mozilla.org/show_bug.cgi?id=1938594#c1
PromiseTestUtils.allowMatchingRejectionsGlobally(
  /Unexpected undefined tabState for onMoveToNewWindow/
);

add_task(async function group_with_windowId() {
  const extension = ExtensionTestUtils.loadExtension({
    async background() {
      const { id: windowId, tabs: initialTabs } = await browser.windows.create(
        {}
      );
      browser.test.assertEq(1, initialTabs.length, "Got window with 1 tab");

      const { id: tabId1 } = await browser.tabs.create({});
      const { id: tabId2 } = await browser.tabs.create({});

      const groupId1 = await browser.tabs.group({
        tabIds: [tabId2, tabId1],
        createProperties: { windowId },
      });

      browser.test.assertDeepEq(
        Array.from(await browser.tabs.query({ groupId: groupId1 }), t => t.id),
        [tabId2, tabId1],
        "Moved tabs to group"
      );
      browser.test.assertDeepEq(
        Array.from(await browser.tabs.query({ windowId }), t => t.id),
        [initialTabs[0].id, tabId2, tabId1],
        "Moved tabs to group in new window (next to initial tab)"
      );

      await browser.windows.remove(windowId);
      browser.test.sendMessage("done");
    },
  });
  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});

add_task(async function group_adopt_from_multiple_windows() {
  const extension = ExtensionTestUtils.loadExtension({
    incognitoOverride: "spanning",
    async background() {
      const {
        id: windowId1,
        tabs: [{ id: tabId1 }],
      } = await browser.windows.create({});
      const {
        id: windowId2,
        tabs: [{ id: tabId2 }],
      } = await browser.windows.create({});
      const {
        id: windowId3,
        tabs: [{ id: tabId3 }],
      } = await browser.windows.create({});

      // This confirms that group() can adapt tabs from different windows.
      const groupId = await browser.tabs.group({
        tabIds: [tabId2, tabId3],
        createProperties: { windowId: windowId1 },
      });

      await browser.test.assertRejects(
        browser.windows.get(windowId2),
        `Invalid window ID: ${windowId2}`,
        "Window closes when group() adopts the last tab of the window"
      );
      // We just confirmed that window2 is closed, do the same for window3.
      await browser.test.assertRejects(
        browser.tabs.group({
          tabIds: tabId1,
          createProperties: { windowId: windowId3 },
        }),
        `Invalid window ID: ${windowId3}`,
        "group() cannot adapt groups from a closed window"
      );

      browser.test.assertDeepEq(
        // Note: tabId1 is missing because the above group() rejected.
        [tabId2, tabId3],
        Array.from(await browser.tabs.query({ groupId }), tab => tab.id),
        "All specified tabIds should now belong to the given group"
      );

      await browser.windows.remove(windowId1);
      browser.test.sendMessage("done");
    },
  });
  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});

add_task(async function group_across_private_browsing_windows() {
  const extension = ExtensionTestUtils.loadExtension({
    incognitoOverride: "spanning",
    async background() {
      const privateWin = await browser.windows.create({ incognito: true });
      const normalWin = await browser.windows.create({ incognito: false });

      const otherPrivateWin = await browser.windows.create({ incognito: true });
      const otherNormalWin = await browser.windows.create({ incognito: false });

      // Mixture of private and non-private tabIDs, so we can easily verify
      // whether we inadvertently move some of the tabs.
      const privateTab = await browser.tabs.create({ windowId: privateWin.id });
      const privateAndNonPrivateTabs = [
        privateTab,
        await browser.tabs.create({ windowId: normalWin.id }),
        await browser.tabs.create({ windowId: privateWin.id }),
        await browser.tabs.create({ windowId: normalWin.id }),
      ];

      await browser.test.assertRejects(
        browser.tabs.group({
          tabIds: privateAndNonPrivateTabs.map(t => t.id),
        }),
        "Cannot move private tabs to non-private window",
        "Should not be able to move private+non-private tabs to current window"
      );

      await browser.test.assertRejects(
        browser.tabs.group({
          tabIds: privateAndNonPrivateTabs.map(t => t.id),
          createProperties: { windowId: otherPrivateWin.id },
        }),
        "Cannot move non-private tabs to private window",
        "Should not be able to move non-private tab to private window"
      );
      await browser.test.assertRejects(
        browser.tabs.group({
          tabIds: privateAndNonPrivateTabs.map(t => t.id),
          createProperties: { windowId: otherNormalWin.id },
        }),
        "Cannot move private tabs to non-private window",
        "Should not be able to move private tab to non-private window"
      );

      const reply = await browser.runtime.sendMessage("@no_private", {
        privateWindowId: privateWin.id,
        privateTabId: privateTab.id,
      });
      browser.test.assertEq("no_private:done", reply, "Reply from other ext");

      for (const tab of privateAndNonPrivateTabs) {
        const actualTab = await browser.tabs.get(tab.id);
        browser.test.assertEq(
          tab.windowId,
          actualTab.windowId,
          "Tab should not have moved to a different window"
        );
        browser.test.assertEq(
          tab.windowId,
          actualTab.windowId,
          "Tab should not have moved within its window"
        );
        browser.test.assertEq(
          tab.groupId,
          -1,
          "Tab should not have joined a group"
        );
      }

      // Now check that we can actually group tabs in private windows.
      const groupId = await browser.tabs.group({
        tabIds: privateTab.id,
        createProperties: { windowId: otherPrivateWin.id },
      });
      const updatedPrivateTab = await browser.tabs.get(privateTab.id);
      browser.test.assertEq(
        groupId,
        updatedPrivateTab.groupId,
        "group() succeeded with private tab"
      );
      browser.test.assertEq(
        otherPrivateWin.id,
        updatedPrivateTab.windowId,
        "Private tab is now part of the destination private window"
      );

      await browser.windows.remove(privateWin.id);
      await browser.windows.remove(normalWin.id);
      await browser.windows.remove(otherPrivateWin.id);
      await browser.windows.remove(otherNormalWin.id);
      browser.test.sendMessage("done");
    },
  });
  const extensionWithoutPrivateAccess = ExtensionTestUtils.loadExtension({
    manifest: { browser_specific_settings: { gecko: { id: "@no_private" } } },
    background() {
      browser.runtime.onMessageExternal.addListener(async data => {
        const { privateWindowId, privateTabId } = data;
        const { id: normalTabId } = await browser.tabs.create({});
        await browser.test.assertRejects(
          browser.tabs.group({ tabIds: [privateTabId] }),
          `Invalid tab ID: ${privateTabId}`,
          "@no_private should not be able to group private tabs"
        );
        await browser.test.assertRejects(
          browser.tabs.group({
            tabIds: [normalTabId],
            createProperties: { windowId: privateWindowId },
          }),
          `Invalid window ID: ${privateWindowId}`,
          "@no_private should not see private windows"
        );
        await browser.tabs.remove(normalTabId);
        return "no_private:done"; // Checked by sender.
      });
    },
  });
  await extensionWithoutPrivateAccess.startup();
  await extension.startup();
  await extension.awaitMessage("done");
  await extensionWithoutPrivateAccess.unload();
  await extension.unload();
});
