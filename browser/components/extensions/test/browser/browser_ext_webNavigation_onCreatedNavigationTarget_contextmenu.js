"use strict";

loadTestSubscript("head_webNavigation.js");

SpecialPowers.pushPrefEnv({
  set: [["security.allow_eval_with_system_principal", true]],
});

async function clickContextMenuItem({
  pageElementSelector,
  contextMenuItemLabel,
  frameIndex,
}) {
  let contentAreaContextMenu;
  if (frameIndex == null) {
    contentAreaContextMenu = await openContextMenu(pageElementSelector);
  } else {
    contentAreaContextMenu = await openContextMenuInFrame(
      pageElementSelector,
      frameIndex
    );
  }
  const item = contentAreaContextMenu.getElementsByAttribute(
    "label",
    contextMenuItemLabel
  );
  is(item.length, 1, `found contextMenu item for "${contextMenuItemLabel}"`);
  const closed = promiseContextMenuClosed(contentAreaContextMenu);
  contentAreaContextMenu.activateItem(item[0]);
  await closed;
}

async function background() {
  const tabs = await browser.tabs.query({ active: true, currentWindow: true });
  const sourceTabId = tabs[0].id;

  const sourceTabFrames = await browser.webNavigation.getAllFrames({
    tabId: sourceTabId,
  });

  browser.webNavigation.onCreatedNavigationTarget.addListener(msg => {
    browser.test.sendMessage("webNavOnCreated", msg);
  });

  browser.webNavigation.onCompleted.addListener(async msg => {
    // NOTE: checking the url is currently necessary because of Bug 1252129
    // ( Filter out webNavigation events related to new window initialization phase).
    if (msg.tabId !== sourceTabId && msg.url !== "about:blank") {
      await browser.tabs.remove(msg.tabId);
      browser.test.sendMessage("webNavOnCompleted", msg);
    }
  });

  browser.tabs.onCreated.addListener(tab => {
    browser.test.sendMessage("tabsOnCreated", tab.id);
  });

  browser.test.sendMessage("expectedSourceTab", {
    sourceTabId,
    sourceTabFrames,
  });
}

add_task(async function test_on_created_navigation_target_from_context_menu() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    SOURCE_PAGE
  );

  const extension = ExtensionTestUtils.loadExtension({
    background,
    manifest: {
      permissions: ["webNavigation"],
    },
  });

  await extension.startup();

  const expectedSourceTab = await extension.awaitMessage("expectedSourceTab");

  info("Open link in a new tab from the context menu");

  await runCreatedNavigationTargetTest({
    extension,
    async openNavTarget() {
      await clickContextMenuItem({
        pageElementSelector: "#test-create-new-tab-from-context-menu",
        contextMenuItemLabel: "Open in New Tab",
      });
    },
    expectedWebNavProps: {
      sourceTabId: expectedSourceTab.sourceTabId,
      sourceFrameId: 0,
      url: `${OPENED_PAGE}#new-tab-from-context-menu`,
    },
  });

  info("Open link in a new window from the context menu");

  await runCreatedNavigationTargetTest({
    extension,
    async openNavTarget() {
      await clickContextMenuItem({
        pageElementSelector: "#test-create-new-window-from-context-menu",
        contextMenuItemLabel: "Open in New Window",
      });
    },
    expectedWebNavProps: {
      sourceTabId: expectedSourceTab.sourceTabId,
      sourceFrameId: 0,
      url: `${OPENED_PAGE}#new-window-from-context-menu`,
    },
  });

  BrowserTestUtils.removeTab(tab);

  await extension.unload();
});

add_task(
  async function test_on_created_navigation_target_from_context_menu_subframe() {
    const tab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      SOURCE_PAGE
    );

    const extension = ExtensionTestUtils.loadExtension({
      background,
      manifest: {
        permissions: ["webNavigation"],
      },
    });

    await extension.startup();

    const expectedSourceTab = await extension.awaitMessage("expectedSourceTab");

    info("Open a subframe link in a new tab from the context menu");

    await runCreatedNavigationTargetTest({
      extension,
      async openNavTarget() {
        await clickContextMenuItem({
          pageElementSelector:
            "#test-create-new-tab-from-context-menu-subframe",
          contextMenuItemLabel: "Open in New Tab",
          frameIndex: 0,
        });
      },
      expectedWebNavProps: {
        sourceTabId: expectedSourceTab.sourceTabId,
        sourceFrameId: expectedSourceTab.sourceTabFrames[1].frameId,
        url: `${OPENED_PAGE}#new-tab-from-context-menu-subframe`,
      },
    });

    info("Open a subframe link in a new window from the context menu");

    await runCreatedNavigationTargetTest({
      extension,
      async openNavTarget() {
        await clickContextMenuItem({
          pageElementSelector:
            "#test-create-new-window-from-context-menu-subframe",
          contextMenuItemLabel: "Open in New Window",
          frameIndex: 0,
        });
      },
      expectedWebNavProps: {
        sourceTabId: expectedSourceTab.sourceTabId,
        sourceFrameId: expectedSourceTab.sourceTabFrames[1].frameId,
        url: `${OPENED_PAGE}#new-window-from-context-menu-subframe`,
      },
    });

    BrowserTestUtils.removeTab(tab);

    await extension.unload();
  }
);
