/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_setup(async function () {
  registerCleanupFunction(() => {
    // reset internal state so it doesn't affect the next tests
    TabsSetupFlowManager.resetInternalState();
  });

  // gSync.init() is called in a requestIdleCallback. Force its initialization.
  gSync.init();

  registerCleanupFunction(async function () {
    await tearDown(gSandbox);
  });

  // set tab sync false so we don't skip setup states
  await SpecialPowers.pushPrefEnv({
    set: [["services.sync.engine.tabs", false]],
  });
});

async function promiseTabListsUpdated({ tabLists }) {
  for (const tabList of tabLists) {
    await tabList.updateComplete;
  }
  await TestUtils.waitForTick();
}

add_task(async function test_unconfigured_initial_state() {
  const sandbox = setupMocks({
    state: UIState.STATUS_NOT_CONFIGURED,
    syncEnabled: false,
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );

    let emptyState =
      syncedTabsComponent.shadowRoot.querySelector("fxview-empty-state");
    ok(
      emptyState.getAttribute("headerlabel").includes("syncedtabs-signin"),
      "Signin message is shown"
    );

    // Test telemetry for signing into Firefox Accounts.
    Services.fog.testResetFOG();
    EventUtils.synthesizeMouseAtCenter(
      emptyState.querySelector(`moz-button[data-action="sign-in"]`).buttonEl,
      {},
      browser.contentWindow
    );
    Assert.equal(
      1,
      Glean.firefoxviewNext.fxaContinueSync.testGetValue().length,
      "Expected one fxa_continue event."
    );
    await BrowserTestUtils.removeTab(
      browser.documentGlobal.gBrowser.selectedTab
    );
  });
  await tearDown(sandbox);
});

add_task(async function test_signed_in() {
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

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Waiting for the synced tabs component to be fully updated"
    );
    let emptyState =
      syncedTabsComponent.shadowRoot.querySelector("fxview-empty-state");
    ok(
      emptyState.getAttribute("headerlabel").includes("syncedtabs-adddevice"),
      "Add device message is shown"
    );

    const mockConnectAdditionDevicesPath = "https://example.com/";
    let expectedUrl =
      "https://support.mozilla.org/kb/how-do-i-set-sync-my-computer#w_connect-additional-devices-to-sync";
    let connectAdditionalDevicesLink =
      emptyState?.shadowRoot.querySelector("a");
    connectAdditionalDevicesLink.scrollIntoView();
    await BrowserTestUtils.waitForMutationCondition(
      emptyState.shadowRoot,
      { subTree: true, childList: true },
      () => BrowserTestUtils.isVisible(connectAdditionalDevicesLink)
    );
    ok(
      BrowserTestUtils.isVisible(connectAdditionalDevicesLink),
      "Support url is visible"
    );
    is(
      connectAdditionalDevicesLink.href,
      expectedUrl,
      "Support link href is correct"
    );
    connectAdditionalDevicesLink.href = mockConnectAdditionDevicesPath;
    info("Mock click on support link");
    let tabPromise = BrowserTestUtils.waitForNewTab(
      gBrowser,
      mockConnectAdditionDevicesPath
    );
    connectAdditionalDevicesLink.click();
    let tab = await tabPromise;
    is(
      tab.linkedBrowser.currentURI.spec,
      mockConnectAdditionDevicesPath,
      "Navigated to mock support link"
    );

    await openFirefoxViewTab(window);
    // Test telemetry for adding a device.
    Services.fog.testResetFOG();
    EventUtils.synthesizeMouseAtCenter(
      emptyState.querySelector(`moz-button[data-action="add-device"]`).buttonEl,
      {},
      browser.contentWindow
    );
    Assert.equal(
      1,
      Glean.firefoxviewNext.fxaMobileSync.testGetValue().length,
      "Expected one fxa_continue event."
    );
    // clean up extra tabs
    while (gBrowser.tabs.length > 1) {
      await BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
    }
  });
  await tearDown(sandbox);
});

add_task(async function test_no_synced_tabs() {
  Services.prefs.setBoolPref("services.sync.engine.tabs", false);
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
      {
        id: 2,
        name: "Other Device",
        type: "desktop",
        tabs: [],
      },
    ],
  });

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Waiting for the synced tabs component to be fully updated"
    );
    let emptyState =
      syncedTabsComponent.shadowRoot.querySelector("fxview-empty-state");
    ok(
      emptyState.getAttribute("headerlabel").includes("syncedtabs-synctabs"),
      "Enable synced tabs message is shown"
    );
  });
  await tearDown(sandbox);
  Services.prefs.setBoolPref("services.sync.engine.tabs", true);
});

add_task(async function test_no_error_for_two_desktop() {
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
      {
        id: 2,
        name: "Other Device",
        type: "desktop",
        tabs: [],
      },
    ],
  });

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Waiting for the synced tabs component to be fully updated"
    );
    let emptyState =
      syncedTabsComponent.shadowRoot.querySelector("fxview-empty-state");
    is(emptyState, null, "No empty state should be shown");
    let noTabs = syncedTabsComponent.shadowRoot.querySelectorAll(".notabs");
    is(noTabs.length, 1, "Should be 1 empty device");
  });
  await tearDown(sandbox);
});

add_task(async function test_empty_state() {
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
      {
        id: 2,
        name: "Other Desktop",
        type: "desktop",
        tabs: [],
      },
      {
        id: 3,
        name: "Other Mobile",
        type: "phone",
        tabs: [],
      },
    ],
  });

  await withFirefoxView({ openNewWindow: true }, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Waiting for the synced tabs component to be fully updated"
    );
    let noTabs = syncedTabsComponent.shadowRoot.querySelectorAll(".notabs");
    is(noTabs.length, 2, "Should be 2 empty devices");

    let headers =
      syncedTabsComponent.shadowRoot.querySelectorAll("h3[slot=header]");
    ok(
      headers[0].textContent.includes("Other Desktop"),
      "Text is correct (Desktop)"
    );
    ok(headers[0].innerHTML.includes("icon desktop"), "Icon should be desktop");
    ok(
      headers[1].textContent.includes("Other Mobile"),
      "Text is correct (Mobile)"
    );
    ok(headers[1].innerHTML.includes("icon phone"), "Icon should be phone");
  });
  await tearDown(sandbox);
});

add_task(async function test_tabs() {
  TabsSetupFlowManager.resetInternalState();

  const sandbox = setupRecentDeviceListMocks();
  const syncedTabsMock = sandbox.stub(SyncedTabs, "getRecentTabs");
  let mockTabs1 = getMockTabData(syncedTabsData1);
  let getRecentTabsResult = mockTabs1;
  syncedTabsMock.callsFake(() => {
    info(
      `Stubbed SyncedTabs.getRecentTabs returning a promise that resolves to ${getRecentTabsResult.length} tabs\n`
    );
    return Promise.resolve(getRecentTabsResult);
  });
  sandbox.stub(SyncedTabs, "getTabClients").callsFake(() => {
    return Promise.resolve(syncedTabsData1);
  });

  await withFirefoxView({ openNewWindow: true }, async browser => {
    // Notify observers while in recent browsing. Once synced tabs is selected,
    // it should have the updated data.
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Waiting for the synced tabs component to be fully updated"
    );

    let headers =
      syncedTabsComponent.shadowRoot.querySelectorAll("h3[slot=header]");
    ok(
      headers[0].textContent.includes("My desktop"),
      "Text is correct (My desktop)"
    );
    ok(headers[0].innerHTML.includes("icon desktop"), "Icon should be desktop");
    ok(
      headers[1].textContent.includes("My iphone"),
      "Text is correct (My iphone)"
    );
    ok(headers[1].innerHTML.includes("icon phone"), "Icon should be phone");

    let tabLists = syncedTabsComponent.tabLists;
    await TestUtils.waitForCondition(() => {
      return tabLists[0].rowEls.length;
    }, "Waiting for the first synced tab list to have row elements");
    let tabRow1 = tabLists[0].rowEls;
    ok(
      tabRow1[0].shadowRoot.textContent.includes,
      "Internet for people, not profits - Mozilla"
    );
    ok(tabRow1[1].shadowRoot.textContent.includes, "Sandboxes - Sinon.JS");
    is(tabRow1.length, 2, "Correct number of rows are displayed.");
    let tabRow2 = tabLists[1].rowEls;
    is(tabRow2.length, 2, "Correct number of rows are dispayed.");
    ok(tabRow1[0].shadowRoot.textContent.includes, "The Guardian");
    ok(tabRow1[1].shadowRoot.textContent.includes, "The Times");

    // Test telemetry for opening a tab.
    Services.fog.testResetFOG();
    EventUtils.synthesizeMouseAtCenter(tabRow1[0], {}, browser.contentWindow);
    const syncEvents = Glean.firefoxviewNext.syncedTabsTabs.testGetValue();
    Assert.equal(1, syncEvents.length, "Expected one synced_tabs event.");
    Assert.deepEqual({ page: "syncedtabs" }, syncEvents[0].extra);
  });
  await tearDown(sandbox);
});

add_task(async function test_empty_desktop_same_name() {
  const sandbox = setupMocks({
    state: UIState.STATUS_SIGNED_IN,
    fxaDevices: [
      {
        id: 1,
        name: "A Device",
        isCurrentDevice: true,
        type: "desktop",
        tabs: [],
      },
      {
        id: 2,
        name: "A Device",
        type: "desktop",
        tabs: [],
      },
    ],
  });

  await withFirefoxView({ openNewWindow: true }, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Waiting for the synced tabs component to be fully updated"
    );
    let noTabs = syncedTabsComponent.shadowRoot.querySelectorAll(".notabs");
    is(noTabs.length, 1, "Should be 1 empty devices");

    let headers =
      syncedTabsComponent.shadowRoot.querySelectorAll("h3[slot=header]");
    ok(
      headers[0].textContent.includes("A Device"),
      "Text is correct (Desktop)"
    );
  });
  await tearDown(sandbox);
});

add_task(async function test_empty_desktop_same_name_three() {
  const sandbox = setupMocks({
    state: UIState.STATUS_SIGNED_IN,
    fxaDevices: [
      {
        id: 1,
        name: "A Device",
        isCurrentDevice: true,
        type: "desktop",
        tabs: [],
      },
      {
        id: 2,
        name: "A Device",
        type: "desktop",
        tabs: [],
      },
      {
        id: 3,
        name: "A Device",
        type: "desktop",
        tabs: [],
      },
    ],
  });

  await withFirefoxView({ openNewWindow: true }, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Waiting for the synced tabs component to be fully updated"
    );
    let noTabs = syncedTabsComponent.shadowRoot.querySelectorAll(".notabs");
    is(noTabs.length, 2, "Should be 2 empty devices");

    let headers =
      syncedTabsComponent.shadowRoot.querySelectorAll("h3[slot=header]");
    ok(
      headers[0].textContent.includes("A Device"),
      "Text is correct (Desktop)"
    );
    ok(
      headers[1].textContent.includes("A Device"),
      "Text is correct (Desktop)"
    );
  });
  await tearDown(sandbox);
});

add_task(async function search_synced_tabs() {
  TabsSetupFlowManager.resetInternalState();

  const sandbox = setupRecentDeviceListMocks();
  const syncedTabsMock = sandbox.stub(SyncedTabs, "getRecentTabs");
  let mockTabs1 = getMockTabData(syncedTabsData1);
  let getRecentTabsResult = mockTabs1;
  syncedTabsMock.callsFake(() => {
    info(
      `Stubbed SyncedTabs.getRecentTabs returning a promise that resolves to ${getRecentTabsResult.length} tabs\n`
    );
    return Promise.resolve(getRecentTabsResult);
  });
  sandbox.stub(SyncedTabs, "getTabClients").callsFake(() => {
    return Promise.resolve(syncedTabsData1);
  });

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "syncedtabs");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Waiting for the synced tabs component to be fully updated"
    );

    is(syncedTabsComponent.cardEls.length, 2, "There are two device cards.");
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list") &&
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length &&
        syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list") &&
        syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list")
          .rowEls.length,
      "The tab list has loaded for the first two cards."
    );
    let deviceOneTabs = syncedTabsComponent.cardEls[0].querySelector(
      "syncedtabs-tab-list"
    ).rowEls;
    let deviceTwoTabs = syncedTabsComponent.cardEls[1].querySelector(
      "syncedtabs-tab-list"
    ).rowEls;

    info("Input a search query.");
    EventUtils.synthesizeMouseAtCenter(
      syncedTabsComponent.searchTextbox,
      {},
      content
    );
    EventUtils.sendString("Mozilla", content);
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Synced Tabs component is done updating."
    );
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list") &&
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length,
      "The tab list has loaded for the first card."
    );
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length === 1,
      "There is one matching search result for the first device."
    );
    await TestUtils.waitForCondition(
      () =>
        !syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list"),
      "There are no matching search results for the second device."
    );

    info("Clear the search query.");
    let clearButton = SpecialPowers.getInputButton(
      syncedTabsComponent.searchTextbox.inputEl
    );
    info(`CLEAR BUTTON: ${clearButton}`);
    EventUtils.synthesizeMouseAtCenter(clearButton, {}, content);
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Synced Tabs component is done updating."
    );
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list") &&
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length &&
        syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list") &&
        syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list")
          .rowEls.length,
      "The tab list has loaded for the first two cards."
    );
    deviceOneTabs = syncedTabsComponent.cardEls[0].querySelector(
      "syncedtabs-tab-list"
    ).rowEls;
    deviceTwoTabs = syncedTabsComponent.cardEls[1].querySelector(
      "syncedtabs-tab-list"
    ).rowEls;
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length === deviceOneTabs.length,
      "The original device's list is restored."
    );
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list")
          .rowEls.length === deviceTwoTabs.length,
      "The new devices's list is restored."
    );
    syncedTabsComponent.searchTextbox.blur();

    info("Input a search query with keyboard.");
    EventUtils.synthesizeKey("f", { accelKey: true }, content);
    EventUtils.sendString("Mozilla", content);
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Synced Tabs component is done updating."
    );
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list") &&
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length,
      "The tab list has loaded for the first card."
    );
    await TestUtils.waitForCondition(() => {
      return (
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length === 1
      );
    }, "There is one matching search result for the first device.");
    await TestUtils.waitForCondition(
      () =>
        !syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list"),
      "There are no matching search results for the second device."
    );

    info("Clear the search query.");
    clearButton = SpecialPowers.getInputButton(
      syncedTabsComponent.searchTextbox.inputEl
    );
    EventUtils.synthesizeMouseAtCenter(clearButton, {}, content);
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Synced Tabs component is done updating."
    );
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list") &&
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length &&
        syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list") &&
        syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list")
          .rowEls.length,
      "The tab list has loaded for the first two cards."
    );
    deviceOneTabs = syncedTabsComponent.cardEls[0].querySelector(
      "syncedtabs-tab-list"
    ).rowEls;
    deviceTwoTabs = syncedTabsComponent.cardEls[1].querySelector(
      "syncedtabs-tab-list"
    ).rowEls;
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[0].querySelector("syncedtabs-tab-list")
          .rowEls.length === deviceOneTabs.length,
      "The original device's list is restored."
    );
    await TestUtils.waitForCondition(
      () =>
        syncedTabsComponent.cardEls[1].querySelector("syncedtabs-tab-list")
          .rowEls.length === deviceTwoTabs.length,
      "The new devices's list is restored."
    );
  });
  await SpecialPowers.popPrefEnv();
  await tearDown(sandbox);
});

add_task(async function search_synced_tabs_recent_browsing() {
  const NUMBER_OF_TABS = 6;
  TabsSetupFlowManager.resetInternalState();
  const sandbox = setupRecentDeviceListMocks();
  const tabClients = [
    {
      id: 1,
      type: "client",
      name: "My desktop",
      clientType: "desktop",
      tabs: Array(NUMBER_OF_TABS)
        .fill()
        .map((_, i) => {
          return {
            type: "tab",
            title: "Internet for people, not profits - Mozilla",
            url: `https://www.mozilla.org/${i}`,
            icon: "https://www.mozilla.org/media/img/favicons/mozilla/favicon.d25d81d39065.ico",
            client: 1,
          };
        }),
    },
    {
      id: 2,
      type: "client",
      name: "My iphone",
      clientType: "phone",
      tabs: [
        {
          type: "tab",
          title: "Mount Everest - Wikipedia",
          url: "https://en.wikipedia.org/wiki/Mount_Everest",
          icon: "https://www.wikipedia.org/static/favicon/wikipedia.ico",
          client: 2,
        },
      ],
    },
  ];
  sandbox
    .stub(SyncedTabs, "getRecentTabs")
    .resolves(getMockTabData(tabClients));
  sandbox.stub(SyncedTabs, "getTabClients").resolves(tabClients);

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentbrowsing");
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    const recentBrowsing = document.querySelector("view-recentbrowsing");
    const slot = recentBrowsing.querySelector("[slot='syncedtabs']");

    // Test that all tab lists repopulate when clearing out searched terms (Bug 1869895 & Bug 1873212)
    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.sendString("Mozilla", content);
    await TestUtils.waitForCondition(
      () => slot.fullyUpdated && slot.tabLists.length === 1,
      "Synced Tabs component is done updating."
    );
    await promiseTabListsUpdated(slot);
    info("Scroll first card into view.");
    slot.tabLists[0].scrollIntoView();
    await TestUtils.waitForCondition(
      () => slot.tabLists[0].rowEls.length === 5,
      "The first card is populated."
    );
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.synthesizeKey("KEY_Backspace", { repeat: 5 });
    await TestUtils.waitForCondition(
      () => slot.fullyUpdated && slot.tabLists.length === 2,
      "Synced Tabs component is done updating."
    );
    await promiseTabListsUpdated(slot);
    info("Scroll second card into view.");
    slot.tabLists[1].scrollIntoView();
    await TestUtils.waitForCondition(
      () =>
        slot.tabLists[0].rowEls.length === 5 &&
        slot.tabLists[1].rowEls.length === 1,
      "Both cards are populated."
    );
    info("Clear the search query.");
    EventUtils.synthesizeKey("KEY_Backspace", { repeat: 2 });

    info("Input a search query");
    EventUtils.synthesizeMouseAtCenter(
      recentBrowsing.searchTextbox,
      {},
      content
    );
    EventUtils.sendString("Mozilla", content);
    await TestUtils.waitForCondition(
      () => slot.fullyUpdated && slot.tabLists.length === 2,
      "Synced Tabs component is done updating."
    );
    await promiseTabListsUpdated(slot);
    await TestUtils.waitForCondition(
      () => slot.tabLists[0].rowEls.length === 5,
      "Not all search results are shown yet."
    );

    info("Click the Show All link.");
    const showAllLink = await TestUtils.waitForCondition(
      () =>
        slot.shadowRoot.querySelector("[data-l10n-id='firefoxview-show-all']"),
      "The show all link to be available in the slot's shadow root"
    );
    is(showAllLink.role, "link", "The show all control is a link.");
    EventUtils.synthesizeMouseAtCenter(showAllLink, {}, content);
    await TestUtils.waitForCondition(
      () => slot.tabLists[0].rowEls.length === NUMBER_OF_TABS,
      "All search results are shown."
    );
    ok(BrowserTestUtils.isHidden(showAllLink), "The show all link is hidden.");
  });
  await SpecialPowers.popPrefEnv();
  await tearDown(sandbox);
});

add_task(async function view_all_synced_tabs_recent_browsing() {
  const NUMBER_OF_TABS = 1;
  TabsSetupFlowManager.resetInternalState();
  const sandbox = setupRecentDeviceListMocks();
  const tabClients = [
    {
      id: 1,
      type: "client",
      name: "My desktop",
      clientType: "desktop",
      tabs: Array(NUMBER_OF_TABS)
        .fill()
        .map((_, i) => {
          return {
            type: "tab",
            title: "Internet for people, not profits - Mozilla",
            url: `https://www.mozilla.org/${i}`,
            icon: "https://www.mozilla.org/media/img/favicons/mozilla/favicon.d25d81d39065.ico",
            client: 1,
          };
        }),
    },
    {
      id: 2,
      type: "client",
      name: "My iphone",
      clientType: "phone",
      tabs: [
        {
          type: "tab",
          title: "Mount Everest - Wikipedia",
          url: "https://en.wikipedia.org/wiki/Mount_Everest",
          icon: "https://www.wikipedia.org/static/favicon/wikipedia.ico",
          client: 2,
        },
      ],
    },
  ];
  sandbox
    .stub(SyncedTabs, "getRecentTabs")
    .resolves(getMockTabData(tabClients));
  sandbox.stub(SyncedTabs, "getTabClients").resolves(tabClients);

  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    await navigateToViewAndWait(document, "recentbrowsing");
    // Notify observers while in recent browsing. Once synced tabs is selected,
    // it should have the updated data.
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);

    const syncedTabsComponent = document.querySelector(
      "view-syncedtabs[slot=syncedtabs]"
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "view-syncedtabs[slot=syncedtabs] is fullyUpdated"
    );

    const viewAllLink =
      syncedTabsComponent.cardEls[0].shadowRoot.querySelector(
        "a.view-all-link"
      );
    ok(
      viewAllLink.href.startsWith(getFirefoxViewURL()),
      `The view all link links to about:firefoxview: ${viewAllLink.href}`
    );

    const openTabsCount = gBrowser.tabs.length;
    const pagesDeck = document.querySelector("named-deck");

    viewAllLink.click();

    await BrowserTestUtils.waitForMutationCondition(
      pagesDeck,
      { attributes: true },
      () => pagesDeck.selectedViewName !== "recentbrowsing"
    );
    is(gBrowser.tabs.length, openTabsCount, "No new tabs were opened");
    Assert.ok(
      FirefoxViewHandler.tab.selected,
      "Firefox View tab is still selected"
    );
    Assert.equal(
      pagesDeck.selectedViewName,
      "syncedtabs",
      "syncedtabs is the selected view after clicking the view-all button"
    );
  });
  await tearDown(sandbox);
});

add_task(async function test_mobile_connected() {
  Services.prefs.setBoolPref("services.sync.engine.tabs", false);
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
      {
        id: 2,
        name: "Other Device",
        type: "mobile",
        tabs: [],
      },
    ],
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    // ensure tab sync is false so we don't skip onto next step
    ok(
      !Services.prefs.getBoolPref("services.sync.engine.tabs", false),
      "services.sync.engine.tabs is initially false"
    );

    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    await navigateToViewAndWait(document, "syncedtabs");

    is(fxAccounts.device.recentDeviceList?.length, 2, "2 devices connected");
    ok(
      fxAccounts.device.recentDeviceList?.some(
        device => device.type == "mobile"
      ),
      "A connected device is type:mobile"
    );
  });
  await tearDown(sandbox);
  Services.prefs.setBoolPref("services.sync.engine.tabs", true);
});

add_task(async function test_tablet_connected() {
  Services.prefs.setBoolPref("services.sync.engine.tabs", false);
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
      {
        id: 2,
        name: "Other Device",
        type: "tablet",
        tabs: [],
      },
    ],
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    // ensure tab sync is false so we don't skip onto next step
    ok(
      !Services.prefs.getBoolPref("services.sync.engine.tabs", false),
      "services.sync.engine.tabs is initially false"
    );

    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    await navigateToViewAndWait(document, "syncedtabs");

    is(fxAccounts.device.recentDeviceList?.length, 2, "2 devices connected");
    ok(
      fxAccounts.device.recentDeviceList?.some(
        device => device.type == "tablet"
      ),
      "A connected device is type:tablet"
    );
  });
  await tearDown(sandbox);
  Services.prefs.setBoolPref("services.sync.engine.tabs", true);
});

add_task(async function test_tab_sync_enabled() {
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
      {
        id: 2,
        name: "Other Device",
        type: "mobile",
        tabs: [],
      },
    ],
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    Services.obs.notifyObservers(null, UIState.ON_UPDATE);
    let syncedTabsComponent = document.querySelector(
      "view-syncedtabs:not([slot=syncedtabs])"
    );

    // test initial state, with the pref not enabled
    await navigateToViewAndWait(document, "syncedtabs");
    // test with the pref toggled on
    Services.prefs.setBoolPref("services.sync.engine.tabs", true);
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Synced tabs component is fully updated."
    );
    ok(!syncedTabsComponent.emptyState, "No empty state is being displayed.");

    // reset and test clicking the action button
    Services.prefs.setBoolPref("services.sync.engine.tabs", false);
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Synced tabs component is fully updated."
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.emptyState,
      "The empty state is rendered."
    );

    const actionButton = syncedTabsComponent.emptyState?.querySelector(
      "moz-button[data-action=sync-tabs-disabled]"
    );
    EventUtils.synthesizeMouseAtCenter(
      actionButton.buttonEl,
      {},
      browser.contentWindow
    );
    await TestUtils.waitForCondition(
      () => syncedTabsComponent.fullyUpdated,
      "Synced tabs component is fully updated."
    );
    await TestUtils.waitForCondition(
      () => !syncedTabsComponent.emptyState,
      "The empty state is rendered."
    );

    ok(true, "The empty state is no longer displayed when sync is enabled");
    ok(
      Services.prefs.getBoolPref("services.sync.engine.tabs", false),
      "tab sync pref should be enabled after button click"
    );
  });
  await tearDown(sandbox);
});
