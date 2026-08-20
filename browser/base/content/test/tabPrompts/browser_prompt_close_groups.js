/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { PromptTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PromptTestUtils.sys.mjs"
);

const { TabStateFlusher } = ChromeUtils.importESModule(
  "moz-src:///browser/components/sessionstore/TabStateFlusher.sys.mjs"
);

const TEST_ROOT = getRootDirectory(gTestPath).replace(
  "chrome://mochitests/content",
  "https://example.com"
);

async function addTab(url) {
  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url,
    animate: false,
  });
  return tab;
}

async function addBeforeUnloadTab() {
  let tab = await addTab(TEST_ROOT + "file_beforeunload_stop.html");
  await BrowserTestUtils.synthesizeMouseAtCenter(
    "body",
    {},
    tab.linkedBrowser.browsingContext
  );
  Assert.ok(
    tab.linkedBrowser.hasBeforeUnload,
    "Added tab has a beforeUnload prompt"
  );
  return tab;
}

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });
});

/**
 * When closing a group with a tab prompt and accepting the prompt,
 * the group should close.
 */
add_task(async function test_closeGroupAndAcceptPrompt() {
  await SimpleTest.requestCompleteLog();
  let tab1 = await addTab("about:mozilla");
  let tab2 = await addBeforeUnloadTab();
  let group = gBrowser.addTabGroup([tab1, tab2]);

  let promptHandled = PromptTestUtils.handleNextPrompt(
    window,
    {
      modalType: Services.prompt.MODAL_TYPE_CONTENT,
    },
    {
      buttonNumClick: 0, // 0 = leave page (accept), 1= stay on page (cancel)
    }
  );

  let groupRemoved = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");

  gBrowser.removeTabGroup(group);
  await Promise.allSettled([promptHandled, groupRemoved]);

  Assert.ok(!gBrowser.getAllTabGroups().length, "Tab group was removed");
});

/**
 * When closing a group with a tab prompt and rejecting the prompt,
 * all tabs in the group should remain open.
 */
add_task(async function test_closeGroupAndRejectPrompt() {
  let tab1 = await addTab("about:mozilla");
  let tab2 = await addBeforeUnloadTab();
  let groupCreated = BrowserTestUtils.waitForEvent(window, "TabGroupCreate");
  let group = gBrowser.addTabGroup([tab1, tab2]);
  await groupCreated;
  let promptHandled = PromptTestUtils.handleNextPrompt(
    tab2.linkedBrowser,
    {
      modalType: Services.prompt.MODAL_TYPE_CONTENT,
    },
    {
      buttonNumClick: 1, // 0 = leave page (accept), 1= stay on page (cancel)
    }
  );

  gBrowser.removeTabGroup(group);
  await promptHandled;

  Assert.equal(gBrowser.getAllTabGroups().length, 1, "Tab group is still open");
  Assert.equal(group.tabs.length, 2, "Both tabs in group remain open");
  let groupRemoved = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  gBrowser.removeTabGroup(group, { skipPermitUnload: true });
  await groupRemoved;
});

/**
 * When closing a group with multiple tab prompts and rejecting ANY of them,
 * all tabs in the group should remain open.
 */
add_task(async function test_closeGroupAndRejectAnyPrompt() {
  // maybe disable this test and try running a11y tests
  let tab1 = await addBeforeUnloadTab();
  let tab2 = await addBeforeUnloadTab();
  let group = gBrowser.addTabGroup([tab1, tab2]);

  let firstPromptHandled = PromptTestUtils.handleNextPrompt(
    tab1.linkedBrowser,
    {
      modalType: Services.prompt.MODAL_TYPE_CONTENT,
    },
    {
      buttonNumClick: 0, // 0 = leave page (accept), 1= stay on page (cancel)
    }
  );

  let secondPromptHandled = PromptTestUtils.handleNextPrompt(
    tab2.linkedBrowser,
    {
      modalType: Services.prompt.MODAL_TYPE_CONTENT,
    },
    {
      buttonNumClick: 1, // 0 = leave page (accept), 1= stay on page (cancel)
    }
  );

  let groupRemoved = gBrowser.removeTabGroup(group);
  await Promise.race([firstPromptHandled, secondPromptHandled]);
  await groupRemoved;

  Assert.equal(gBrowser.getAllTabGroups().length, 1, "Tab group is still open");
  Assert.equal(group.tabs.length, 2, "Both tabs in group remain open");
  groupRemoved = BrowserTestUtils.waitForEvent(group, "TabGroupRemoved");
  gBrowser.removeTabGroup(group, { skipPermitUnload: true });
  await groupRemoved;
});

/**
 * When a bunch of tabs are closed, e.g. when using "close tabs to the right",
 * any whole tabgroups with beforeunload handlers should be kept open if
 * cancelled by the user.
 */
add_task(async function test_closeMultipleTabsAndRejectFromGroup() {
  let group1Tab1 = await addTab("about:mozilla");
  let group1Tab2 = await addBeforeUnloadTab();
  let group2Tab1 = await addBeforeUnloadTab();
  let group2Tab2 = await addTab("about:mozilla");
  let group1 = gBrowser.addTabGroup([group1Tab1, group1Tab2], {
    id: "leave-open",
    label: "leave open",
  });
  gBrowser.addTabGroup([group2Tab1, group2Tab2], {
    id: "close",
    label: "close",
  });

  let firstPromptHandled = PromptTestUtils.handleNextPrompt(
    group1Tab2.linkedBrowser,
    {
      modalType: Services.prompt.MODAL_TYPE_CONTENT,
    },
    {
      buttonNumClick: 1, // 0 = leave page (accept), 1= stay on page (cancel)
    }
  );

  let secondPromptHandled = PromptTestUtils.handleNextPrompt(
    group2Tab1.linkedBrowser,
    {
      modalType: Services.prompt.MODAL_TYPE_CONTENT,
    },
    {
      buttonNumClick: 0, // 0 = leave page (accept), 1= stay on page (cancel)
    }
  );

  gBrowser.removeTabsToTheEndFrom(gBrowser.tabs[0]);
  await Promise.allSettled([firstPromptHandled, secondPromptHandled]);

  // initial tab and 1 tab group should remain open
  await TestUtils.waitForCondition(
    () => gBrowser.tabs.length == 3,
    "3 tabs remain open"
  );
  Assert.equal(
    gBrowser.getAllTabGroups()[0].id,
    group1.id,
    "Group 1 is still open"
  );

  await gBrowser.removeTabGroup(group1, { skipPermitUnload: true });
});

/**
 * When a tab group is saved & closed, it should not end up saved if the user
 * cancels the group close.
 */
add_task(async function test_cancelClosingASavingGroup() {
  let tab1 = await addBeforeUnloadTab();
  let tab2 = await addTab("about:mozilla");
  await TabStateFlusher.flush(tab1.linkedBrowser);
  let group = gBrowser.addTabGroup([tab1, tab2]);
  group.save();
  Assert.equal(
    SessionStore.getCurrentState().savedGroups.length,
    1,
    "Group is saved"
  );

  let promptHandled = PromptTestUtils.handleNextPrompt(
    tab1.linkedBrowser,
    {
      modalType: Services.prompt.MODAL_TYPE_CONTENT,
    },
    {
      buttonNumClick: 1, // 0 = leave page (accept), 1= stay on page (cancel)
    }
  );
  let groupRemoved = gBrowser.removeTabGroup(group);
  await Promise.allSettled([promptHandled, groupRemoved]);
  Assert.equal(gBrowser.getAllTabGroups().length, 1, "Group is still open");
  Assert.ok(
    !SessionStore.getCurrentState().savedGroups.length,
    "Group is no longer saved after close was canceled"
  );
  await gBrowser.removeTabGroup(group, { skipPermitUnload: true });
});
