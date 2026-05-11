const { PlacesTestUtils } = ChromeUtils.importESModule(
  "resource://testing-common/PlacesTestUtils.sys.mjs"
);

/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["test.wait300msAfterTabSwitch", true]],
  });

  registerCleanupFunction(async () => {
    await SpecialPowers.popPrefEnv();
    clearHistory();
  });
});

add_task(async function firefox_view_entered_telemetry() {
  Services.fog.testResetFOG();
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    let selectedEvents =
      Glean.firefoxviewNext.tabSelectedToolbarbutton.testGetValue();
    Assert.equal(1, selectedEvents.length, "Expected 1 selected event.");
    let enteredEvents = Glean.firefoxviewNext.enteredFirefoxview.testGetValue();
    Assert.equal(1, enteredEvents.length, "Expected 1 entered event.");
    Assert.deepEqual({ page: "recentbrowsing" }, enteredEvents[0].extra);

    await navigateToViewAndWait(document, "recentlyclosed");
    Services.fog.testResetFOG();
    await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:robots");
    is(
      gBrowser.selectedBrowser.currentURI.spec,
      "about:robots",
      "The selected tab is about:robots"
    );
    await switchToFxViewTab(browser.documentGlobal);

    selectedEvents =
      Glean.firefoxviewNext.tabSelectedToolbarbutton.testGetValue();
    Assert.equal(1, selectedEvents.length, "Expected 1 selected event.");
    enteredEvents = Glean.firefoxviewNext.enteredFirefoxview.testGetValue();
    Assert.equal(1, enteredEvents.length, "Expected 1 entered event.");
    Assert.deepEqual({ page: "recentlyclosed" }, enteredEvents[0].extra);

    await SpecialPowers.popPrefEnv();
    // clean up extra tabs
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
    }
  });
});

add_task(async function test_collapse_and_expand_card() {
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;

    // Test using Recently Closed card on Recent Browsing page
    let recentlyClosedComponent = document.querySelector(
      "view-recentlyclosed[slot=recentlyclosed]"
    );
    await TestUtils.waitForCondition(
      () => recentlyClosedComponent.fullyUpdated,
      "The recently closed component to be fully updated"
    );
    let cardContainer = recentlyClosedComponent.cardEl;
    is(
      cardContainer.isExpanded,
      true,
      "The card-container is expanded initially"
    );
    Services.fog.testResetFOG();
    // Click the summary to collapse the details disclosure
    EventUtils.synthesizeMouseAtCenter(cardContainer.summaryEl, {}, content);
    is(
      cardContainer.detailsEl.hasAttribute("open"),
      false,
      "The card-container is collapsed"
    );
    await Services.fog.testFlushAllChildren();
    Assert.equal(
      1,
      Glean.firefoxviewNext.cardCollapsedCardContainer.testGetValue().length
    );
    // Click the summary again to expand the details disclosure
    EventUtils.synthesizeMouseAtCenter(cardContainer.summaryEl, {}, content);
    is(
      cardContainer.detailsEl.hasAttribute("open"),
      true,
      "The card-container is expanded"
    );
    await Services.fog.testFlushAllChildren();
    Assert.equal(
      1,
      Glean.firefoxviewNext.cardExpandedCardContainer.testGetValue().length
    );
  });
});

add_task(async function test_change_page_telemetry() {
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    Services.fog.testResetFOG();
    await navigateToViewAndWait(document, "recentlyclosed");
    let changePageEvents =
      Glean.firefoxviewNext.changePageNavigation.testGetValue();
    Assert.equal(1, changePageEvents.length, "Expected 1 change page event.");
    Assert.deepEqual(
      { page: "recentlyclosed", source: "category-navigation" },
      changePageEvents[0].extra
    );
    await navigateToViewAndWait(document, "recentbrowsing");

    let openTabsComponent = document.querySelector(
      "view-opentabs[slot=opentabs]"
    );
    let cardContainer =
      openTabsComponent.shadowRoot.querySelector("view-opentabs-card").cardEl;
    let viewAllLink = cardContainer.viewAllLink;
    Services.fog.testResetFOG();
    EventUtils.synthesizeMouseAtCenter(viewAllLink, {}, content);
    changePageEvents =
      Glean.firefoxviewNext.changePageNavigation.testGetValue();
    Assert.equal(1, changePageEvents.length, "Expected 1 change page event.");
    Assert.deepEqual(
      { page: "opentabs", source: "view-all" },
      changePageEvents[0].extra
    );
  });
});

add_task(async function test_browser_context_menu_telemetry() {
  const menu = document.getElementById("contentAreaContextMenu");
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    Services.fog.testResetFOG();

    // Test browser context menu options
    const openTabsComponent = document.querySelector("view-opentabs");
    await TestUtils.waitForCondition(
      () =>
        openTabsComponent.shadowRoot.querySelector("view-opentabs-card").tabList
          .rowEls.length,
      "open tabs card tab list to have row elements"
    );
    const [openTabsRow] =
      openTabsComponent.shadowRoot.querySelector("view-opentabs-card").tabList
        .rowEls;
    const promisePopup = BrowserTestUtils.waitForEvent(menu, "popupshown");
    EventUtils.synthesizeMouseAtCenter(
      openTabsRow,
      { type: "contextmenu" },
      content
    );
    await promisePopup;
    const promiseNewWindow = BrowserTestUtils.waitForNewWindow();
    menu.activateItem(menu.querySelector("#context-openlink"));

    await TestUtils.waitForCondition(
      () => Glean.firefoxviewNext.browserContextMenuTabs.testGetValue(),
      "Context-menu event arrives."
    );
    const contextEvents =
      Glean.firefoxviewNext.browserContextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Expected 1 context-menu event.");
    Assert.deepEqual(
      { menu_action: "context-openlink", page: "recentbrowsing" },
      contextEvents[0].extra
    );

    // Clean up extra window
    const win = await promiseNewWindow;
    await BrowserTestUtils.closeWindow(win);
  });
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_context_menu_new_window_telemetry() {
  await PlacesUtils.history.insert({
    url: URLs[0],
    title: "Example Domain 1",
    visits: [{ date: new Date() }],
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    is(
      document.location.href,
      "about:firefoxview",
      "The Recent browsing page is showing."
    );

    // Test history context menu options
    await navigateToViewAndWait(document, "history");
    let historyComponent = document.querySelector("view-history");
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "The history component to be fully updated"
    );
    await TestUtils.waitForCondition(
      () => historyComponent.lists[0].rowEls.length,
      "Waiting for the first history list to have row elements"
    );
    let firstTabList = historyComponent.lists[0];
    let firstItem = firstTabList.rowEls[0];
    let panelList = historyComponent.panelList;
    EventUtils.synthesizeMouseAtCenter(
      firstItem.secondaryButtonEl,
      {},
      content
    );
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    Services.fog.testResetFOG();
    let panelItems = Array.from(panelList.children).filter(
      panelItem => panelItem.nodeName === "PANEL-ITEM"
    );
    let openInNewWindowOption = panelItems[2];
    let newWindowPromise = BrowserTestUtils.waitForNewWindow({
      url: URLs[0],
    });
    EventUtils.synthesizeMouseAtCenter(openInNewWindowOption, {}, content);
    let win = await newWindowPromise;
    const contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Got one context menu event.");
    Assert.deepEqual(
      { menu_action: "open-in-new-window", data_type: "history" },
      contextEvents[0].extra
    );
    await BrowserTestUtils.closeWindow(win);
    info("New window closed.");

    // clean up extra tabs
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
    }
  });
});

add_task(async function test_context_menu_private_window_telemetry() {
  await PlacesUtils.history.insert({
    url: URLs[0],
    title: "Example Domain 1",
    visits: [{ date: new Date() }],
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    is(
      document.location.href,
      "about:firefoxview",
      "The Recent browsing page is showing."
    );

    // Test history context menu options
    await navigateToViewAndWait(document, "history");
    let historyComponent = document.querySelector("view-history");
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );
    await TestUtils.waitForCondition(
      () => historyComponent.lists[0].rowEls.length,
      "Waiting for the first history list to have row elements"
    );
    let firstTabList = historyComponent.lists[0];
    let firstItem = firstTabList.rowEls[0];
    let panelList = historyComponent.panelList;
    EventUtils.synthesizeMouseAtCenter(
      firstItem.secondaryButtonEl,
      {},
      content
    );
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    let panelItems = Array.from(panelList.children).filter(
      panelItem => panelItem.nodeName === "PANEL-ITEM"
    );

    EventUtils.synthesizeMouseAtCenter(
      firstItem.secondaryButtonEl,
      {},
      content
    );
    info("Context menu button clicked.");
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    info("Context menu shown.");
    Services.fog.testResetFOG();
    let openInPrivateWindowOption = panelItems[3];
    let newWindowPromise = BrowserTestUtils.waitForNewWindow({
      url: URLs[0],
    });
    EventUtils.synthesizeMouseAtCenter(openInPrivateWindowOption, {}, content);
    info("Open in private window context menu option clicked.");
    let win = await newWindowPromise;
    info("New private window opened.");
    const contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Got one context menu event.");
    Assert.deepEqual(
      { menu_action: "open-in-private-window", data_type: "history" },
      contextEvents[0].extra
    );
    ok(
      PrivateBrowsingUtils.isWindowPrivate(win),
      "Should have opened a private window."
    );
    await BrowserTestUtils.closeWindow(win);
    info("New private window closed.");

    // clean up extra tabs
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
    }
  });
});

add_task(async function test_context_menu_delete_from_history_telemetry() {
  await PlacesUtils.history.clear();
  await PlacesUtils.history.insert({
    url: URLs[0],
    title: "Example Domain 1",
    visits: [{ date: new Date() }],
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    is(
      document.location.href,
      "about:firefoxview",
      "The Recent browsing page is showing."
    );

    // Test history context menu options
    await navigateToViewAndWait(document, "history");
    let historyComponent = document.querySelector("view-history");
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "The history component to be fully updated"
    );
    await TestUtils.waitForCondition(
      () => historyComponent.lists[0].rowEls.length,
      "Waiting for the first history list to have row elements"
    );
    let firstTabList = historyComponent.lists[0];
    let firstItem = firstTabList.rowEls[0];
    let panelList = historyComponent.panelList;
    EventUtils.synthesizeMouseAtCenter(
      firstItem.secondaryButtonEl,
      {},
      content
    );
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    let panelItems = Array.from(panelList.children).filter(
      panelItem => panelItem.nodeName === "PANEL-ITEM"
    );

    EventUtils.synthesizeMouseAtCenter(
      firstItem.secondaryButtonEl,
      {},
      content
    );
    info("Context menu button clicked.");
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    info("Context menu shown.");
    Services.fog.testResetFOG();
    let deleteFromHistoryOption = panelItems[0];
    ok(
      deleteFromHistoryOption.textContent.includes("Delete"),
      "Delete from history button is present in the context menu."
    );
    EventUtils.synthesizeMouseAtCenter(deleteFromHistoryOption, {}, content);
    info("Delete from history context menu option clicked.");

    await TestUtils.waitForCondition(
      () =>
        !historyComponent.paused &&
        historyComponent.fullyUpdated &&
        !historyComponent.lists.length,
      "The history component to be fully updated, unpaused, and have no lists"
    );
    const contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Got one context menu event.");
    Assert.deepEqual(
      { menu_action: "delete-from-history", data_type: "history" },
      contextEvents[0].extra
    );

    // clean up extra tabs
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
    }
  });
});

add_task(async function test_context_menu_forget_about_this_site_telemetry() {
  await PlacesUtils.history.clear();
  await PlacesUtils.history.insert({
    url: URLs[0],
    title: "Example Domain 1",
    visits: [{ date: new Date() }],
  });
  await withFirefoxView({}, async browser => {
    const { document } = browser.contentWindow;
    is(
      document.location.href,
      "about:firefoxview",
      "The Recent browsing page is showing."
    );
    await navigateToViewAndWait(document, "history");
    let historyComponent = document.querySelector("view-history");
    await TestUtils.waitForCondition(
      () => historyComponent.fullyUpdated,
      "Waiting for the history component to be fully updated"
    );
    await TestUtils.waitForCondition(
      () => historyComponent.lists[0].rowEls.length,
      "Waiting for the first history list to have row elements"
    );
    let firstTabList = historyComponent.lists[0];
    let firstItem = firstTabList.rowEls[0];
    let panelList = historyComponent.panelList;
    EventUtils.synthesizeMouseAtCenter(
      firstItem.secondaryButtonEl,
      {},
      content
    );
    info("Context menu button clicked.");
    await BrowserTestUtils.waitForEvent(panelList, "shown");
    info("Context menu shown.");
    Services.fog.testResetFOG();
    let panelItems = Array.from(panelList.children).filter(
      panelItem => panelItem.nodeName === "PANEL-ITEM"
    );
    let forgetAboutThisSiteOption = panelItems[1];
    ok(
      forgetAboutThisSiteOption.textContent.includes("Forget"),
      "Forget About This Site button is present in the context menu."
    );
    let dialogOpened = BrowserTestUtils.promiseAlertDialogOpen(
      null,
      "chrome://browser/content/places/clearDataForSite.xhtml",
      { isSubDialog: true }
    );
    EventUtils.synthesizeMouseAtCenter(forgetAboutThisSiteOption, {}, content);
    info("Forget About This Site context menu option clicked.");
    let dialog = await dialogOpened;
    info("Dialog opened.");
    let removeButton = dialog.document
      .querySelector("dialog")
      .getButton("accept");
    removeButton.click();
    info("Clear Data button clicked.");
    await BrowserTestUtils.waitForEvent(dialog, "unload");
    const contextEvents = Glean.firefoxviewNext.contextMenuTabs.testGetValue();
    Assert.equal(1, contextEvents.length, "Got one context menu event.");
    Assert.deepEqual(
      { menu_action: "forget-about-this-site", data_type: "history" },
      contextEvents[0].extra
    );
    // clean up extra tabs
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.tabs.at(-1));
    }
  });
});
