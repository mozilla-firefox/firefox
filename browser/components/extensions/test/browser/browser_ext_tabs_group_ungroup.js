/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

// TODO bug 1938594: group_pinned_tab_from_different_window sometimes triggers
// this error. See https://bugzilla.mozilla.org/show_bug.cgi?id=1938594#c1
PromiseTestUtils.allowMatchingRejectionsGlobally(
  /Unexpected undefined tabState for onMoveToNewWindow/
);

add_task(async function group_ungroup_and_index() {
  const extension = ExtensionTestUtils.loadExtension({
    manifest: {
      permissions: ["tabGroups"],
    },
    files: {
      "tab1.htm": "<title>tab1.html</title>",
      "tab2.htm": "<title>tab2.html</title>",
      "tab3.htm": "<title>tab3.html</title>",
    },
    async background() {
      const { id: tabId1 } = await browser.tabs.create({ url: "tab1.htm" });
      const { id: tabId2 } = await browser.tabs.create({ url: "tab2.htm" });
      const { id: tabId3 } = await browser.tabs.create({ url: "tab3.htm" });

      let eventIds = [];
      let expected = [];
      let allEvents = Promise.withResolvers();

      browser.tabGroups.onCreated.addListener(group => {
        eventIds.push(group.id);
        browser.test.log(`Events so far (${eventIds.length}): ${eventIds}`);
      });
      browser.tabGroups.onRemoved.addListener(group => {
        eventIds.push(-group.id);
        browser.test.log(`Events so far (${eventIds.length}): ${eventIds}`);
        if (eventIds.length === 16) {
          allEvents.resolve();
        }
        if (eventIds.length > 16) {
          browser.fail("Extra event received: " + group.id);
        }
      });

      async function assertAllTabExpectations(expectations, desc) {
        const tabs = await Promise.all([
          browser.tabs.get(tabId1),
          browser.tabs.get(tabId2),
          browser.tabs.get(tabId3),
        ]);
        const { indexes, groupIds, ...rest } = expectations;
        if (Object.keys(rest).length) {
          // Sanity check, so we don't miss expectations due to typos.
          throw new Error(
            `Unexpected keys in expectations: ${Object.keys(rest)} for ${desc}`
          );
        }
        browser.test.assertDeepEq(
          indexes,
          tabs.map(t => t.index),
          `${desc} : Tabs should be at expected indexes`
        );
        browser.test.assertDeepEq(
          groupIds,
          tabs.toSorted((a, b) => a.index - b.index).map(t => t.groupId),
          `${desc} : Tab groupIds in order of tabs`
        );
      }

      await assertAllTabExpectations(
        { indexes: [1, 2, 3], groupIds: [-1, -1, -1] },
        "Initial tab indexes and group IDs before group()"
      );

      const groupId1 = await browser.tabs.group({ tabIds: tabId1 });
      const groupId2 = await browser.tabs.group({ tabIds: [tabId2, tabId3] });
      expected.push(groupId1, groupId2);

      await assertAllTabExpectations(
        { indexes: [1, 2, 3], groupIds: [groupId1, groupId2, groupId2] },
        "Got two groups after group(tab1) + group([tab2, tab3])"
      );

      // It should be possible to ungroup tabs from different groups.
      // And their position should not move, since these tabs are not in the
      // middle of a tab group.
      await browser.tabs.ungroup([tabId3, tabId1]);
      await browser.tabs.ungroup(tabId2);
      expected.push(-groupId1, -groupId2);

      await browser.test.assertRejects(
        browser.tabs.group({ tabIds: tabId3, groupId: groupId1 }),
        `No group with id: ${groupId1}`,
        "After ungrouping, the groupId should no longer be valid"
      );

      await assertAllTabExpectations(
        { indexes: [1, 2, 3], groupIds: [-1, -1, -1] },
        "Tab groups should still be at their original position in the tab"
      );

      // Now grouping two tabs that are apart - they should be together.
      const groupId3 = await browser.tabs.group({ tabIds: [tabId1, tabId3] });
      await assertAllTabExpectations(
        { indexes: [1, 3, 2], groupIds: [groupId3, groupId3, -1] },
        "Tabs in same tab group must be next to each other"
      );
      expected.push(groupId3);

      // Join existing tab group - now we should have three in the tab group.
      const groupId4 = await browser.tabs.group({
        tabIds: [tabId2],
        groupId: groupId3,
      });
      browser.test.assertEq(
        groupId3,
        groupId4,
        "group() with a groupId parameter returns given groupId"
      );

      // Joining an existing group should not have changed positions.
      await assertAllTabExpectations(
        { indexes: [1, 3, 2], groupIds: [groupId3, groupId3, groupId3] },
        "Tab order did not change when joining adjacent group"
      );

      await browser.tabs.ungroup([tabId1, tabId2, tabId3]);
      expected.push(-groupId3);

      // Ungrouping of the group should not have changed positions either,
      // despite the list of tabIds passed to ungroup() being out of order.
      await assertAllTabExpectations(
        { indexes: [1, 3, 2], groupIds: [-1, -1, -1] },
        "Tab order did not change when ungrouping with out-of-order tabIds"
      );

      // Group tabs together. Tab positions should match given order.
      const groupId5 = await browser.tabs.group({
        tabIds: [tabId1, tabId2, tabId3],
      });
      await assertAllTabExpectations(
        { indexes: [1, 2, 3], groupIds: [groupId5, groupId5, groupId5] },
        "Tab order matches order of tabIds passed to tabs.group()"
      );

      // Move the leftmost tab to a new group. That tab should still be
      // positioned at the left of the original tab group.
      const groupId6 = await browser.tabs.group({ tabIds: [tabId1] });
      await assertAllTabExpectations(
        { indexes: [1, 2, 3], groupIds: [groupId6, groupId5, groupId5] },
        "Leftmost tab should still be ordered before the original tab group"
      );
      expected.push(groupId5, groupId6);

      // Join an existing group (from the left). Position should not change.
      await browser.tabs.group({ tabIds: [tabId1], groupId: groupId5 });
      await assertAllTabExpectations(
        { indexes: [1, 2, 3], groupIds: [groupId5, groupId5, groupId5] },
        "Tab order did not change when joining a tab group from the left"
      );
      await browser.test.assertRejects(
        browser.tabs.group({ tabIds: tabId3, groupId: groupId6 }),
        `No group with id: ${groupId6}`,
        "Old groupId should be invalid after last tab was moved from group"
      );
      expected.push(-groupId6);

      // Move the middle tab to a new group. That tab should be at the right.
      const groupId7 = await browser.tabs.group({ tabIds: [tabId2] });
      await assertAllTabExpectations(
        { indexes: [1, 3, 2], groupIds: [groupId5, groupId5, groupId7] },
        "group() on middle tab in existing group appears on the right"
      );

      // Prepare: tabId1 and tabId2 together at the left, followed by tabId3.
      const groupId8 = await browser.tabs.group({ tabIds: [tabId1, tabId2] });
      await browser.tabs.ungroup(tabId3);
      expected.push(groupId7, groupId8);

      // When tabId2 is moved to a new group, it should stay in the middle,
      // meaning that the tab was inserted after its original tab group.
      // In particular, it should not move to the end of the tab strip.
      const groupId9 = await browser.tabs.group({ tabIds: [tabId2] });
      await assertAllTabExpectations(
        { indexes: [1, 2, 3], groupIds: [groupId8, groupId9, -1] },
        "group() on rightmost tab should appear after original tab group"
      );
      expected.push(-groupId7, -groupId5, groupId9);

      await browser.tabs.remove(tabId1);
      await browser.tabs.remove(tabId2);
      await browser.tabs.remove(tabId3);

      expected.push(-groupId8, -groupId9);

      // TODO bug 1962683: Re-enable when events are no longer missing
      // await allEvents.promise;
      //
      // browser.test.assertEq(
      //   eventIds.join(),
      //   expected.join(),
      //   "Received expected onCreated events"
      // );
      browser.test.log(`Expect: ${eventIds.join()}`);
      browser.test.log(`Actual: ${expected.join()}`);

      browser.test.sendMessage("done");
    },
  });
  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});

add_task(async function group_pinned_tab() {
  const extension = ExtensionTestUtils.loadExtension({
    async background() {
      const { id: tabId1 } = await browser.tabs.create({ pinned: true });
      const { id: tabId2 } = await browser.tabs.create({ pinned: true });
      const groupId = await browser.tabs.group({ tabIds: [tabId1] });
      browser.test.assertTrue(groupId > 0, `group() created group: ${groupId}`);

      const tab1 = await browser.tabs.get(tabId1);
      browser.test.assertFalse(tab1.pinned, "group() unpins tab 1");
      browser.test.assertEq(groupId, tab1.groupId, "group() grouped tab 1");

      const groupId2 = await browser.tabs.group({ tabIds: [tabId2], groupId });
      browser.test.assertEq(groupId, groupId2, "group() existing group");

      const tab2 = await browser.tabs.get(tabId2);
      browser.test.assertFalse(tab2.pinned, "group() unpins tab 2");
      browser.test.assertEq(groupId, tab2.groupId, "Tab joined existing group");

      await browser.tabs.remove(tabId1);
      await browser.tabs.remove(tabId2);
      browser.test.sendMessage("done");
    },
  });
  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});

add_task(async function group_pinned_tab_from_different_window() {
  const extension = ExtensionTestUtils.loadExtension({
    async background() {
      const { id: tabId } = await browser.tabs.create({ pinned: true });
      const { id: windowId } = await browser.windows.create({});
      const groupId = await browser.tabs.group({
        tabIds: [tabId],
        createProperties: { windowId },
      });
      browser.test.assertTrue(groupId > 0, `group() created group: ${groupId}`);

      const tab = await browser.tabs.get(tabId);
      browser.test.assertFalse(tab.pinned, "group() unpins tab");
      browser.test.assertEq(groupId, tab.groupId, "group() grouped tab");
      browser.test.assertEq(windowId, tab.windowId, "Moved tab to new window");

      await browser.windows.remove(windowId);
      browser.test.sendMessage("done");
    },
  });
  await extension.startup();
  await extension.awaitMessage("done");
  await extension.unload();
});
