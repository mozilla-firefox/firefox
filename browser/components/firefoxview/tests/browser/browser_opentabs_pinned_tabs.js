/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

let pageWithAlert =
  // eslint-disable-next-line @microsoft/sdl/no-insecure-url
  "http://example.com/browser/browser/base/content/test/tabPrompts/openPromptOffTimeout.html";
let pageWithSound =
  "http://mochi.test:8888/browser/dom/base/test/file_audioLoop.html";

function cleanup() {
  // Cleanup
  while (gBrowser.tabs.length > 1) {
    BrowserTestUtils.removeTab(gBrowser.tabs[1]);
  }
}

const arrowDown = async tabList => {
  info("Arrow down");
  EventUtils.synthesizeKey("KEY_ArrowDown", {});
  await tabList.getUpdateComplete();
};
const arrowUp = async tabList => {
  info("Arrow up");
  EventUtils.synthesizeKey("KEY_ArrowUp", {});
  await tabList.getUpdateComplete();
};
const arrowRight = async tabList => {
  info("Arrow right");
  EventUtils.synthesizeKey("KEY_ArrowRight", {});
  await tabList.getUpdateComplete();
};
const arrowLeft = async tabList => {
  info("Arrow left");
  EventUtils.synthesizeKey("KEY_ArrowLeft", {});
  await tabList.getUpdateComplete();
};

add_task(async function test_pin_unpin_open_tab() {
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "opentabs");

    let openTabs = document.querySelector("view-opentabs[name=opentabs]");
    await openTabs.updateComplete;
    await TestUtils.waitForCondition(
      () => openTabs.viewCards[0].tabList.rowEls.length,
      "Waiting for the first view card tab list to have row elements"
    );
    await openTabs.openTabsTarget.readyWindowsPromise;
    let card = openTabs.viewCards[0];
    let openTabEl = card.tabList.rowEls[0];
    let tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    // Pin tab
    EventUtils.synthesizeMouseAtCenter(
      openTabEl.secondaryButtonEl,
      {},
      content
    );
    await TestUtils.waitForCondition(
      () => card.tabContextMenu.panelList,
      "Waiting for the tab context menu panel list to be available"
    );
    let panelList = card.tabContextMenu.panelList;
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    info("The context menu is shown when clicking the tab's 'more' button");

    let pinTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-pin-tab]"
    );

    tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    // Unpin tab
    EventUtils.synthesizeMouseAtCenter(pinTabPanelItem, {}, content);
    info("Pin Tab context menu option clicked.");

    await tabChangeRaised;
    await openTabs.updateComplete;

    let pinnedTab = card.tabList.rowEls[0];
    await TestUtils.waitForCondition(
      () => pinnedTab.indicators.includes("pinned"),
      "Waiting for the pinned tab to have the pinned indicator"
    );

    // Check aria roles
    let listWrapper = card.tabList.shadowRoot.querySelector(".fxview-tab-list");
    ok(
      Array.from(listWrapper.classList).includes("pinned"),
      "The tab list has the 'pinned' class as expected."
    );

    Assert.strictEqual(
      listWrapper.getAttribute("role"),
      "tablist",
      "The list wrapper has an aria-role of 'tablist'"
    );
    Assert.strictEqual(
      pinnedTab.pinnedTabButtonEl.getAttribute("role"),
      "tab",
      "The pinned tab's button element has a role of 'tab'"
    );

    // Open context menu
    EventUtils.synthesizeMouseAtCenter(
      pinnedTab,
      { type: "contextmenu" },
      content
    );
    await TestUtils.waitForCondition(
      () => card.tabContextMenu.panelList,
      "Waiting for the tab context menu panel list to be available"
    );
    panelList = card.tabContextMenu.panelList;
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    info("The context menu is shown when right clicking the pinned tab");

    let unpinTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-unpin-tab]"
    );

    Services.fog.testResetFOG();
    tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    // Unpin tab
    EventUtils.synthesizeMouseAtCenter(unpinTabPanelItem, {}, content);
    info("Unpin Tab context menu option clicked.");

    await tabChangeRaised;
    await openTabs.updateComplete;
    const contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Expect one context menu event.");
    Assert.deepEqual(
      { menu_action: "unpin-tab", data_type: "opentabs" },
      contextEvents[0].extra
    );
  });
  cleanup();
});

add_task(async function test_indicator_pinned_tabs_with_keyboard() {
  await add_new_tab(URLs[0]);
  await add_new_tab(URLs[1]);
  await add_new_tab(pageWithSound);
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "opentabs");

    let openTabs = document.querySelector("view-opentabs[name=opentabs]");
    await openTabs.updateComplete;
    await TestUtils.waitForCondition(
      () => openTabs.viewCards[0].tabList.rowEls.length,
      "Waiting for the first view card tab list to have row elements"
    );
    await openTabs.openTabsTarget.readyWindowsPromise;
    setSortOption(openTabs, "tabStripOrder");
    let card = openTabs.viewCards[0];

    let openedTab = await BrowserTestUtils.openNewForegroundTab(
      gBrowser,
      pageWithAlert,
      true
    );
    let openedTabGotAttentionPromise = BrowserTestUtils.waitForAttribute(
      "attention",
      openedTab
    );

    await switchToFxViewTab();

    await openedTabGotAttentionPromise;

    let tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    // Pin 2 of 5 tabs
    browser.documentGlobal.gBrowser.tabs.forEach((tab, i) => {
      if (i > 2 && tab != browser.documentGlobal.FirefoxViewHandler.tab) {
        browser.documentGlobal.gBrowser.pinTab(tab);
      }
    });

    await tabChangeRaised;
    await openTabs.updateComplete;

    let soundPlayingPinnedTab = card.tabList.rowEls[0];
    let attentionPinnedTab = card.tabList.rowEls[1];
    let firstUnpinnedTab = card.tabList.rowEls[2];
    let secondUnpinnedTab = card.tabList.rowEls[3];

    // Check soundplaying indicator
    ok(
      soundPlayingPinnedTab.indicators.includes("pinned") &&
        soundPlayingPinnedTab.indicators.includes("soundplaying") &&
        soundPlayingPinnedTab.mediaButtonEl,
      "The first pinned tab has the 'sound playing' indicator."
    );

    soundPlayingPinnedTab.pinnedTabButtonEl.focus();
    ok(
      isActiveElement(soundPlayingPinnedTab.pinnedTabButtonEl),
      "Focus should be on the first pinned tab's button element."
    );
    info("First pinned tab has focus");

    // Test mute/unmute
    EventUtils.synthesizeKey("m", { ctrlKey: true });
    await TestUtils.waitForCondition(
      () => soundPlayingPinnedTab.indicators.includes("muted"),
      "Waiting for the sound playing pinned tab to have the muted indicator"
    );
    EventUtils.synthesizeKey("m", { ctrlKey: true });
    await TestUtils.waitForCondition(
      () => !soundPlayingPinnedTab.indicators.includes("muted"),
      "Waiting for the sound playing pinned tab to no longer have the muted indicator"
    );

    await arrowRight(card.tabList);
    ok(
      isActiveElement(attentionPinnedTab.pinnedTabButtonEl),
      "Focus should be on the second pinned tab's button element."
    );

    // Check notification dot indicator
    ok(
      attentionPinnedTab.indicators.includes("pinned") &&
        attentionPinnedTab.indicators.includes("attention") &&
        Array.from(
          attentionPinnedTab.shadowRoot.querySelector(
            ".fxview-tab-row-favicon-wrapper"
          ).classList
        ).includes("attention"),
      "The second pinned tab has the 'attention' indicator."
    );

    await arrowDown(card.tabList);
    ok(
      isActiveElement(firstUnpinnedTab.mainEl),
      "Focus should be on the first unpinned tab's main/link element."
    );

    await arrowRight(card.tabList);
    ok(
      isActiveElement(firstUnpinnedTab.secondaryButtonEl),
      "Focus should be on the first unpinned tab's secondary/more button element."
    );

    await arrowUp(card.tabList);
    ok(
      isActiveElement(attentionPinnedTab.pinnedTabButtonEl),
      "Focus should be on the second pinned tab's button element."
    );

    await arrowRight(card.tabList);
    ok(
      isActiveElement(firstUnpinnedTab.secondaryButtonEl),
      "Focus should be on the first unpinned tab's secondary/more button element."
    );

    await arrowUp(card.tabList);
    ok(
      isActiveElement(attentionPinnedTab.pinnedTabButtonEl),
      "Focus should be on the second pinned tab's button element."
    );

    await arrowLeft(card.tabList);
    ok(
      isActiveElement(soundPlayingPinnedTab.pinnedTabButtonEl),
      "Focus should be on the first pinned tab's button element."
    );

    await arrowDown(card.tabList);
    ok(
      isActiveElement(firstUnpinnedTab.secondaryButtonEl),
      "Focus should be on the first unpinned tab's secondary/more button element."
    );

    await arrowDown(card.tabList);
    ok(
      isActiveElement(secondUnpinnedTab.secondaryButtonEl),
      "Focus should be on the second unpinned tab's secondary/more button element."
    );

    // Switch back to other tab to close prompt before cleanup
    await BrowserTestUtils.switchTab(gBrowser, openedTab);
    EventUtils.synthesizeKey("KEY_Enter", {});
  });
  cleanup();
});

add_task(async function test_mute_unmute_pinned_tab() {
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "opentabs");

    let openTabs = document.querySelector("view-opentabs[name=opentabs]");
    await openTabs.updateComplete;
    await TestUtils.waitForCondition(
      () => openTabs.viewCards[0].tabList.rowEls.length,
      "Waiting for the first view card tab list to have row elements"
    );
    await openTabs.openTabsTarget.readyWindowsPromise;
    let card = openTabs.viewCards[0];
    let openTabEl = card.tabList.rowEls[0];
    let tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    // Mute tab
    // We intentionally turn off this a11y check, because the following click
    // is purposefully targeting a not focusable button within a pinned tab
    // control. A keyboard-only user could mute/unmute this pinned tab via the
    // context menu, while we do not want to create an additional, unnecessary
    // tabstop for this control, therefore this rule check shall be ignored by
    // a11y_checks suite.
    AccessibilityUtils.setEnv({ focusableRule: false });
    EventUtils.synthesizeMouseAtCenter(openTabEl.mediaButtonEl, {}, content);
    AccessibilityUtils.resetEnv();
    info("Mute Tab button clicked.");

    tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    await tabChangeRaised;
    await openTabs.updateComplete;

    let mutedTab = card.tabList.rowEls[0];
    await TestUtils.waitForCondition(
      () => mutedTab.indicators.includes("muted"),
      "Waiting for the muted tab to have the muted indicator"
    );

    // Unmute tab
    // We intentionally turn off this a11y check, because the following click
    // is purposefully targeting a not focusable button within a pinned tab
    // control. A keyboard-only user could mute/unmute this pinned tab via the
    // context menu, while we do not want to create an additional, unnecessary
    // tabstop for this control, therefore this rule check shall be ignored by
    // a11y_checks suite.
    AccessibilityUtils.setEnv({ focusableRule: false });
    EventUtils.synthesizeMouseAtCenter(openTabEl.mediaButtonEl, {}, content);
    AccessibilityUtils.resetEnv();
    info("Unmute Tab button clicked.");

    tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    await tabChangeRaised;
    await openTabs.updateComplete;

    let unmutedTab = card.tabList.rowEls[0];
    await TestUtils.waitForCondition(
      () => !unmutedTab.indicators.includes("muted"),
      "Waiting for the unmuted tab to no longer have the muted indicator"
    );
  });
  cleanup();
});

add_task(async function test_mute_unmute_with_context_menu() {
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "opentabs");

    let openTabs = document.querySelector("view-opentabs[name=opentabs]");
    await openTabs.updateComplete;
    await TestUtils.waitForCondition(
      () => openTabs.viewCards[0].tabList.rowEls.length,
      "Waiting for the first view card tab list to have row elements"
    );
    await openTabs.openTabsTarget.readyWindowsPromise;
    let card = openTabs.viewCards[0];
    let openTabEl = card.tabList.rowEls[0];
    let tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    // Mute tab
    EventUtils.synthesizeMouseAtCenter(
      openTabEl.pinnedTabButtonEl,
      { type: "contextmenu" },
      content
    );
    await TestUtils.waitForCondition(
      () => card.tabContextMenu.panelList,
      "Waiting for the tab context menu panel list to be available"
    );
    let panelList = card.tabContextMenu.panelList;
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    info("The context menu is shown when clicking the tab's 'more' button");

    let pinTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-mute-tab]"
    );

    tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    // Mute tab
    EventUtils.synthesizeMouseAtCenter(pinTabPanelItem, {}, content);
    info("Mute Tab context menu option clicked.");

    await tabChangeRaised;
    await openTabs.updateComplete;

    let mutedTab = card.tabList.rowEls[0];
    await TestUtils.waitForCondition(
      () => mutedTab.indicators.includes("muted"),
      "Waiting for the muted tab to have the muted indicator"
    );

    // Open context menu
    EventUtils.synthesizeMouseAtCenter(
      mutedTab,
      { type: "contextmenu" },
      content
    );
    await TestUtils.waitForCondition(
      () => card.tabContextMenu.panelList,
      "Waiting for the tab context menu panel list to be available"
    );
    panelList = card.tabContextMenu.panelList;
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    info("The context menu is shown when right clicking the pinned tab");

    let unmuteTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-unmute-tab]"
    );

    Services.fog.testResetFOG();
    tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );

    // Unpin tab
    EventUtils.synthesizeMouseAtCenter(unmuteTabPanelItem, {}, content);
    info("Unmute Tab context menu option clicked.");

    await tabChangeRaised;
    await openTabs.updateComplete;
    const contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Expect one context menu event.");
    Assert.deepEqual(
      { menu_action: "unmute-tab", data_type: "opentabs" },
      contextEvents[0].extra
    );

    let unmutedTab = card.tabList.rowEls[0];
    await TestUtils.waitForCondition(
      () => !unmutedTab.indicators.includes("muted"),
      "Waiting for the unmuted tab to no longer have the muted indicator"
    );
  });
  cleanup();
});
