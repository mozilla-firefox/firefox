/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

// Test regularly times out - especially with verify
requestLongerTimeout(2);

const TEST_URL1 = "about:robots";
const TEST_URL2 = "https://example.org/";
const TEST_URL3 = "about:mozilla";

const fxaDevicesWithCommands = [
  {
    id: 1,
    name: "My desktop device",
    availableCommands: { "https://identity.mozilla.com/cmd/open-uri": "test" },
    lastAccessTime: Date.now(),
  },
  {
    id: 2,
    name: "My mobile device",
    availableCommands: { "https://identity.mozilla.com/cmd/open-uri": "boo" },
    lastAccessTime: Date.now() + 60000, // add 30min
  },
];

function getVisibleTabURLs(win = window) {
  return win.gBrowser.visibleTabs.map(tab => tab.linkedBrowser.currentURI.spec);
}

function getTabRowURLs(rows) {
  return Array.from(rows).map(row => row.url);
}

async function waitUntilRowsMatch(openTabs, cardIndex, expectedURLs) {
  let card;

  info(
    "moreMenuSetup: openTabs has openTabsTarget?:" + !!openTabs?.openTabsTarget
  );
  //await openTabs.openTabsTarget.readyWindowsPromise;
  info(
    `waitUntilRowsMatch, wait for there to be at least ${cardIndex + 1} cards`
  );
  await BrowserTestUtils.waitForCondition(() => {
    if (!openTabs.initialWindowsReady) {
      info("openTabs.initialWindowsReady isn't true");
      return false;
    }
    try {
      card = getOpenTabsCards(openTabs)[cardIndex];
    } catch (ex) {
      info("Calling getOpenTabsCards produced exception: " + ex.message);
    }
    return !!card;
  }, "Waiting for openTabs to be ready and to get the cards");

  const expectedURLsAsString = JSON.stringify(expectedURLs);
  info(`Waiting for row URLs to match ${expectedURLs.join(", ")}`);
  await BrowserTestUtils.waitForMutationCondition(
    card.shadowRoot,
    { characterData: true, childList: true, subtree: true },
    async () => {
      let rows = await getTabRowsForCard(card);
      return (
        rows.length == expectedURLs.length &&
        JSON.stringify(getTabRowURLs(rows)) == expectedURLsAsString
      );
    }
  );
}

async function getContextMenuPanelListForCard(card) {
  let menuContainer = card.shadowRoot.querySelector(
    "view-opentabs-contextmenu"
  );
  ok(menuContainer, "Found the menuContainer for card");
  await TestUtils.waitForCondition(
    () => menuContainer.panelList,
    "Waiting for the context menu's panel-list to be rendered"
  );
  ok(
    menuContainer.panelList,
    "Found the panelList in the card's view-opentabs-contextmenu"
  );
  return menuContainer.panelList;
}

async function openContextMenuForItem(tabItem, card) {
  const root = card.shadowRoot;
  // Wait for <panel-list> "shown" inside shadowRoot and return live node
  const shownPromise = BrowserTestUtils.waitForEvent(root, "shown", true, e => {
    const panelList = e
      .composedPath()
      .find(node => node?.localName === "panel-list");
    if (panelList && panelList.isConnected) {
      e._panelList = panelList;
      return true;
    }
    return false;
  });
  // click on the item's button element (more menu)
  // and wait for the panel list to be shown
  tabItem.secondaryButtonEl.click();
  // NOTE: menu must populate with devices data before it can be rendered
  // so the creation of the panel-list can be async
  const event = await shownPromise;
  return event._panelList || (await getContextMenuPanelListForCard(card));
}

async function moreMenuSetup(urls) {
  for (let url of urls) {
    await add_new_tab(url);
  }

  // once we've opened a few tabs, navigate to the open tabs section in firefox view
  await clickFirefoxViewButton(window);
  const document = window.FirefoxViewHandler.tab.linkedBrowser.contentDocument;

  await navigateToViewAndWait(document, "opentabs");

  let openTabs = document.querySelector("view-opentabs[name=opentabs]");
  setSortOption(openTabs, "tabStripOrder");
  await openTabs.openTabsTarget.readyWindowsPromise;

  info("waiting for openTabs' first card rows");
  await waitUntilRowsMatch(openTabs, 0, getVisibleTabURLs());

  let cards = getOpenTabsCards(openTabs);
  is(cards.length, 1, "There is one open window.");

  let rows = await getTabRowsForCard(cards[0]);

  let firstTab = rows[0];

  firstTab.scrollIntoView();
  is(
    isElInViewport(firstTab),
    true,
    "first tab list item is visible in viewport"
  );

  return [cards, rows];
}

add_task(async function test_close_open_tab() {
  await withFirefoxView({}, async () => {
    const [cards, rows] = await moreMenuSetup([TEST_URL2, TEST_URL3]);
    const firstTab = rows[0];
    const tertiaryButtonEl = firstTab.tertiaryButtonEl;

    ok(tertiaryButtonEl, "Dismiss button exists");

    Services.fog.testResetFOG();

    let tabsUpdated = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );
    EventUtils.synthesizeMouseAtCenter(tertiaryButtonEl, {}, content);
    await tabsUpdated;
    Assert.deepEqual(
      getVisibleTabURLs(),
      [TEST_URL2, TEST_URL3],
      "First tab successfully removed"
    );

    const closeEvents = Glean.firefoxviewNext.closeOpenTabTabs.testGetValue();
    Assert.equal(1, closeEvents.length, "Expected one close tab event.");

    const openTabs = cards[0].ownerDocument.querySelector(
      "view-opentabs[name=opentabs]"
    );
    await waitUntilRowsMatch(openTabs, 0, [TEST_URL2, TEST_URL3]);

    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs[0]);
    }
  });
});

add_task(async function test_more_menus() {
  await withFirefoxView({}, async browser => {
    let win = browser.documentGlobal;
    let shown, menuHidden;

    gBrowser.selectedTab = gBrowser.visibleTabs[0];
    Assert.equal(
      gBrowser.selectedTab.linkedBrowser.currentURI.spec,
      "about:mozilla",
      "Selected tab is about:mozilla"
    );

    info(`Loading ${TEST_URL1} into the selected about:mozilla tab`);
    let tabLoaded = BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);

    win.gURLBar.focus();
    win.gURLBar.value = TEST_URL1;
    EventUtils.synthesizeKey("KEY_Enter", {}, win);
    await tabLoaded;

    info("Waiting for moreMenuSetup to resolve");
    let [cards, rows] = await moreMenuSetup([TEST_URL2, TEST_URL3]);
    Assert.deepEqual(
      getVisibleTabURLs(),
      [TEST_URL1, TEST_URL2, TEST_URL3],
      "Prepared 3 open tabs"
    );

    // Move Tab submenu item
    let firstTab = rows[0];
    // Open the panel list (more menu) from the first list item
    let panelList = await openContextMenuForItem(firstTab, cards[0]);

    let openTabs = cards[0].ownerDocument.querySelector(
      "view-opentabs[name=opentabs]"
    );
    await waitUntilRowsMatch(openTabs, 0, [TEST_URL1, TEST_URL2, TEST_URL3]);

    is(firstTab.url, TEST_URL1, `First tab list item is ${TEST_URL1}`);

    let moveTabsPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-move-tab]"
    );

    let moveTabsSubmenuList = moveTabsPanelItem.shadowRoot.querySelector(
      "panel-list[id=move-tab-menu]"
    );
    ok(moveTabsSubmenuList, "Move tabs submenu panel list exists");

    // navigate to the "Move tabs" submenu option, and
    // open it with the right arrow key
    shown = BrowserTestUtils.waitForEvent(moveTabsSubmenuList, "shown");
    EventUtils.synthesizeKey("KEY_ArrowRight", {});
    await shown;

    Services.fog.testResetFOG();

    // click on the first option, which should be "Move to the end" since
    // this is the first tab
    menuHidden = BrowserTestUtils.waitForEvent(panelList, "hidden");
    let tabChangeRaised = BrowserTestUtils.waitForEvent(
      NonPrivateTabs,
      "TabChange"
    );
    EventUtils.synthesizeKey("KEY_Enter", {});
    info("Waiting for result of moving a tab via the menu");
    let contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Expected one close tab event.");
    Assert.deepEqual(
      { menu_action: "move-tab-end", data_type: "opentabs" },
      contextEvents[0].extra
    );
    await menuHidden;
    await tabChangeRaised;

    Assert.deepEqual(
      getVisibleTabURLs(),
      [TEST_URL2, TEST_URL3, TEST_URL1],
      "The last tab became the first tab"
    );

    // this entire "move tabs" submenu test can be reordered above
    // closing a tab since it very clearly reveals the issues
    // outlined in bug 1852622 when there are 3 or more tabs open
    // and one is moved via the more menus.
    await waitUntilRowsMatch(openTabs, 0, [TEST_URL2, TEST_URL3, TEST_URL1]);

    // Copy Link menu item (copyLink function that's called is a member of Viewpage.mjs)
    panelList = await openContextMenuForItem(firstTab, cards[0]);
    firstTab = rows[0];
    let panelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-copy-link]"
    );
    let panelItemButton = panelItem.shadowRoot.querySelector(
      "button[role=menuitem]"
    );
    ok(panelItem, "Copy link panel item exists");
    ok(
      panelItemButton,
      "Copy link panel item button with role=menuitem exists"
    );

    Services.fog.testResetFOG();

    menuHidden = BrowserTestUtils.waitForEvent(panelList, "hidden");
    panelItemButton.click();
    info("Waiting for menuHidden");
    await menuHidden;
    contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Expected one close tab event.");
    Assert.deepEqual(
      { menu_action: "copy-link", data_type: "opentabs" },
      contextEvents[0].extra
    );

    let copiedText = SpecialPowers.getClipboardData(
      "text/plain",
      Ci.nsIClipboard.kGlobalClipboard
    );
    is(copiedText, TEST_URL2, "The correct url has been copied and pasted");

    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs[0]);
    }
  });
});

add_task(async function test_send_device_submenu() {
  const sandbox = setupMocks({
    state: UIState.STATUS_SIGNED_IN,
    fxaDevices: [
      {
        id: 1,
        name: "This Device",
        isCurrentDevice: true,
        type: "desktop",
        tabs: [],
      },
    ],
  });
  sandbox
    .stub(gSync, "getSendTabTargets")
    .callsFake(() => fxaDevicesWithCommands);

  await withFirefoxView({}, async () => {
    // TEST_URL1 is our only tab, left over from previous test
    Assert.deepEqual(
      getVisibleTabURLs(),
      [TEST_URL1],
      `We initially have a single ${TEST_URL1} tab`
    );
    let shown;

    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    let [cards, rows] = await moreMenuSetup([TEST_URL2, TEST_URL3]);

    let firstTab = rows[0];
    let panelList = await openContextMenuForItem(firstTab, cards[0]);

    let sendTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-send-to-device]"
    );

    ok(sendTabPanelItem, "Send tabs to device submenu panel item exists");
    Assert.equal(
      sendTabPanelItem.getAttribute("data-l10n-id"),
      "fxviewtabrow-send-to-device"
    );

    let sendTabSubmenuList = sendTabPanelItem.shadowRoot.querySelector(
      "panel-list[id=send-tab-menu]"
    );
    ok(sendTabSubmenuList, "Send tabs to device submenu panel list exists");

    // navigate down to the "Send tabs" submenu option, and
    // open it with the right arrow key
    EventUtils.synthesizeKey("KEY_ArrowDown", { repeat: 4 });

    shown = BrowserTestUtils.waitForEvent(sendTabSubmenuList, "shown");
    EventUtils.synthesizeKey("KEY_ArrowRight", {});
    await shown;

    let expectation = sandbox
      .mock(gSync)
      .expects("sendTabToDevice")
      .once()
      .withExactArgs(
        { url: TEST_URL1, title: "Gort! Klaatu barada nikto!", private: false },
        [fxaDevicesWithCommands[0]]
      )
      .returns(true);

    Services.fog.testResetFOG();

    // click on the first device and verify it was "sent"
    let menuHidden = BrowserTestUtils.waitForEvent(panelList, "hidden");
    EventUtils.synthesizeKey("KEY_Enter", {});

    expectation.verify();
    const contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Expected one close tab event.");
    Assert.deepEqual(
      { menu_action: "send-tab-device", data_type: "opentabs" },
      contextEvents[0].extra
    );
    await menuHidden;

    sandbox.restore();
    TabsSetupFlowManager.resetInternalState();
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs[0]);
    }
  });
});

add_task(async function test_send_mobile_submenu_text() {
  const sandbox = setupMocks({
    state: UIState.STATUS_SIGNED_IN,
    fxaDevices: [
      {
        id: 1,
        name: "This Device",
        isCurrentDevice: true,
        type: "desktop",
        tabs: [],
      },
    ],
  });
  const fxaMobileDeviceWithCommands = [
    {
      id: 2,
      name: "My mobile device",
      type: "mobile",
      availableCommands: { "https://identity.mozilla.com/cmd/open-uri": "boo" },
      lastAccessTime: Date.now() + 60000, // add 30min
    },
    {
      id: 3,
      name: "My tablet device",
      type: "tablet",
      availableCommands: { "https://identity.mozilla.com/cmd/open-uri": "boo" },
      lastAccessTime: Date.now() + 60000, // add 30min
    },
  ];
  sandbox
    .stub(gSync, "getSendTabTargets")
    .callsFake(() => fxaMobileDeviceWithCommands);

  await withFirefoxView({}, async () => {
    // TEST_URL3 is our only tab, left over from previous test
    Assert.deepEqual(
      getVisibleTabURLs(),
      [TEST_URL3],
      `We initially have a single ${TEST_URL3} tab`
    );

    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    let [cards, rows] = await moreMenuSetup([TEST_URL2]);

    let firstTab = rows[0];
    let panelList = await openContextMenuForItem(firstTab, cards[0]);

    let sendTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-send-to-mobile]"
    );

    ok(sendTabPanelItem, "Send tabs to mobile submenu panel item exists");
    Assert.equal(
      sendTabPanelItem.getAttribute("data-l10n-id"),
      "fxviewtabrow-send-to-mobile"
    );

    sandbox.restore();
    TabsSetupFlowManager.resetInternalState();
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs[0]);
    }
  });
});

add_task(async function test_send_mobile_fxa_disabled_submenu_text() {
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.enabled", false]],
  });

  const sandbox = setupMocks({
    state: UIState.STATUS_NOT_CONFIGURED,
    fxaDevices: [
      {
        id: 1,
        name: "This Device",
        isCurrentDevice: true,
        type: "desktop",
        tabs: [],
      },
    ],
  });
  sandbox.stub(gSync, "getSendTabTargets").callsFake(() => []);

  await withFirefoxView({}, async () => {
    // TEST_URL2 is our only tab, left over from previous test
    Assert.deepEqual(
      getVisibleTabURLs(),
      [TEST_URL2],
      `We initially have a single ${TEST_URL2} tab`
    );

    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    let [cards, rows] = await moreMenuSetup([TEST_URL3]);

    let firstTab = rows[0];
    let panelList = await openContextMenuForItem(firstTab, cards[0]);

    let sendTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-send-to-mobile]"
    );

    ok(
      !sendTabPanelItem,
      "Send tabs to mobile submenu panel item does not exist"
    );

    sandbox.restore();
    TabsSetupFlowManager.resetInternalState();
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs[0]);
    }
  });
});

add_task(async function test_send_mobile_signed_out_submenu_text() {
  await SpecialPowers.pushPrefEnv({
    set: [["identity.fxaccounts.enabled", true]],
  });

  const sandbox = setupMocks({
    state: UIState.STATUS_NOT_CONFIGURED,
    fxaDevices: [
      {
        id: 1,
        name: "This Device",
        isCurrentDevice: true,
        type: "desktop",
        tabs: [],
      },
    ],
  });
  sandbox.stub(gSync, "getSendTabTargets").callsFake(() => []);

  await withFirefoxView({}, async () => {
    // TEST_URL3 is our only tab, left over from previous test
    Assert.deepEqual(
      getVisibleTabURLs(),
      [TEST_URL3],
      `We initially have a single ${TEST_URL3} tab`
    );

    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    let [cards, rows] = await moreMenuSetup([TEST_URL2]);

    let firstTab = rows[0];
    let panelList = await openContextMenuForItem(firstTab, cards[0]);

    let sendTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-send-to-mobile]"
    );

    ok(sendTabPanelItem, "Send tabs to mobile submenu panel item exists");

    sandbox.restore();
    TabsSetupFlowManager.resetInternalState();
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs[0]);
    }
  });
});

add_task(
  async function test_send_mobile_signed_in_sync_disabled_submenu_text() {
    const sandbox = setupMocks({
      state: UIState.STATUS_SIGNED_IN,
      fxaDevices: [
        {
          id: 1,
          name: "This Device",
          isCurrentDevice: true,
          type: "desktop",
          tabs: [],
        },
      ],
    });
    sandbox.stub(gSync, "getSendTabTargets").callsFake(() => []);
    sandbox.stub(gSync, "isSignedInWithSyncDisabled").get(() => true);

    await withFirefoxView({}, async () => {
      // TEST_URL2 is our only tab, left over from previous test
      Assert.deepEqual(
        getVisibleTabURLs(),
        [TEST_URL2],
        `We initially have a single ${TEST_URL2} tab`
      );

      Services.obs.notifyObservers(null, UIState.ON_UPDATE);
      let [cards, rows] = await moreMenuSetup([TEST_URL3]);

      let firstTab = rows[0];
      let panelList = await openContextMenuForItem(firstTab, cards[0]);

      let sendTabPanelItem = panelList.querySelector(
        "panel-item[data-l10n-id=fxviewtabrow-send-to-mobile]"
      );

      ok(sendTabPanelItem, "Send tabs to mobile panel item exists");

      let sendTabSubmenuList = sendTabPanelItem.shadowRoot.querySelector(
        "panel-list[id=send-tab-menu1]"
      );
      ok(sendTabSubmenuList, "Send tabs to mobile submenu panel list exists");

      let enableSyncPanelItem = sendTabSubmenuList.querySelector(
        "panel-item[data-l10n-id=fxviewtabrow-send-to-mobile-enable-sync2]"
      );

      ok(
        enableSyncPanelItem,
        "Send tabs to mobile enable sync panel item exists"
      );

      sandbox.restore();
      TabsSetupFlowManager.resetInternalState();
      while (gBrowser.tabs.length > 1) {
        BrowserTestUtils.removeTab(gBrowser.tabs[0]);
      }
    });
  }
);

add_task(async function test_send_mobile_single_device_submenu_text() {
  const sandbox = setupMocks({
    state: UIState.STATUS_SIGNED_IN,
    fxaDevices: [
      {
        id: 1,
        name: "This Device",
        isCurrentDevice: true,
        type: "desktop",
        tabs: [],
      },
    ],
  });
  sandbox.stub(gSync, "getSendTabTargets").callsFake(() => []);
  sandbox.stub(gSync, "hasNoSendTabTargets").get(() => true);

  await withFirefoxView({}, async () => {
    // TEST_URL3 is our only tab, left over from previous test
    Assert.deepEqual(
      getVisibleTabURLs(),
      [TEST_URL3],
      `We initially have a single ${TEST_URL3} tab`
    );

    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    let [cards, rows] = await moreMenuSetup([TEST_URL2]);

    let firstTab = rows[0];
    let panelList = await openContextMenuForItem(firstTab, cards[0]);

    let sendTabPanelItem = panelList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-send-to-mobile]"
    );

    ok(sendTabPanelItem, "Send tabs to mobile panel item exists");

    let sendTabSubmenuList = sendTabPanelItem.shadowRoot.querySelector(
      "panel-list[id=send-tab-menu2]"
    );
    ok(sendTabSubmenuList, "Send tabs to mobile submenu panel list exists");

    let connectPhonePanelItem = sendTabSubmenuList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-send-to-mobile-connect-phone2]"
    );

    ok(
      connectPhonePanelItem,
      "Send tabs to mobile connect phone panel item exists"
    );

    let deviceMissingPanelItem = sendTabSubmenuList.querySelector(
      "panel-item[data-l10n-id=fxviewtabrow-send-to-mobile-device-missing2]"
    );

    ok(
      deviceMissingPanelItem,
      "Send tabs to mobile device missing panel item exists"
    );

    sandbox.restore();
    TabsSetupFlowManager.resetInternalState();
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs[0]);
    }
  });
});
