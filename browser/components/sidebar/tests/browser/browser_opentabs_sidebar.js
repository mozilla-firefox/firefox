/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_setup(async () => {
  await SpecialPowers.pushPrefEnv({
    set: [["sidebar.openTabsPanel.enabled", true]],
  });
});

async function showOpenTabsPanel() {
  await SidebarTestUtils.showPanel(window, "viewOpenTabsSidebar");
  const { contentDocument } = SidebarController.browser;
  return contentDocument.querySelector("sidebar-opentabs");
}

function getTabList(component) {
  return component.shadowRoot.querySelector("sidebar-tab-list");
}

// Mirror the filtering done by OpenTabsController.getTabListItems.
function getVisibleTabCount() {
  return gBrowser.openTabs.filter(tab => !tab.hidden && !tab.closing).length;
}

async function waitForRowCount(tabList, expected) {
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => tabList.rowEls?.length === expected
  );
}

add_task(async function test_opentabs_lists_current_window_tabs() {
  const tab1 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  const tab2 = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  const expected = getVisibleTabCount();
  await waitForRowCount(tabList, expected);

  Assert.equal(
    tabList.rowEls.length,
    expected,
    "Row count matches the number of visible tabs in the window."
  );

  BrowserTestUtils.removeTab(tab1);
  BrowserTestUtils.removeTab(tab2);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_opentabs_row_tooltip() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,<title>Tooltip Test</title>"
  );
  const url = tab.linkedBrowser.currentURI.spec;

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  await waitForRowCount(tabList, getVisibleTabCount());

  const row = [...tabList.rowEls].find(rowEl => rowEl.tabElement === tab);
  await assertRowTooltip(
    row,
    `Tooltip Test\n${url}`,
    "Tooltip shows the tab title and URL."
  );

  BrowserTestUtils.removeTab(tab);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_clicking_row_selects_tab() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  // Select a different tab so clicking the row actually changes selection.
  gBrowser.selectedTab = gBrowser.tabs[0];

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  await waitForRowCount(tabList, getVisibleTabCount());

  const row = [...tabList.rowEls].find(rowEl => rowEl.tabElement === tab);
  Assert.ok(row, "Found the row for the opened tab.");

  const content = SidebarController.browser.contentWindow;
  await content.promiseDocumentFlushed(() => {});

  AccessibilityUtils.setEnv({ focusableRule: false });
  await EventUtils.synthesizeMouseAtCenter(row.mainEl, { button: 0 }, content);
  AccessibilityUtils.resetEnv();

  Assert.equal(
    gBrowser.selectedTab,
    tab,
    "Clicking a row selects the corresponding tab."
  );

  BrowserTestUtils.removeTab(tab);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_list_updates_on_open_and_close() {
  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);

  const initialCount = getVisibleTabCount();
  await waitForRowCount(tabList, initialCount);

  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  await waitForRowCount(tabList, initialCount + 1);
  Assert.equal(
    tabList.rowEls.length,
    initialCount + 1,
    "Opening a tab adds a row."
  );

  BrowserTestUtils.removeTab(tab);
  await waitForRowCount(tabList, initialCount);
  Assert.equal(
    tabList.rowEls.length,
    initialCount,
    "Closing a tab removes its row."
  );

  SidebarTestUtils.closePanel(window);
});

add_task(async function test_close_button_closes_tab() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  const initialCount = getVisibleTabCount();

  const row = await TestUtils.waitForCondition(
    () => [...tabList.rowEls].find(rowEl => rowEl.url === "about:robots"),
    "Row for the opened tab should render."
  );

  const content = SidebarController.browser.contentWindow;
  await content.promiseDocumentFlushed(() => {});

  // Hover the row so its close button is interactable, then click it.
  await EventUtils.synthesizeMouseAtCenter(
    row.mainEl,
    { type: "mouseover" },
    content
  );
  const closeButton = await TestUtils.waitForCondition(
    () => row.secondaryButtonEl,
    "Close button should render on the row."
  );
  EventUtils.synthesizeMouseAtCenter(closeButton, {}, content);

  await TestUtils.waitForCondition(
    () => !gBrowser.tabs.includes(tab),
    "Clicking the close button should close the tab."
  );
  await waitForRowCount(tabList, initialCount - 1);

  SidebarTestUtils.closePanel(window);
});

add_task(async function test_pinned_tabs_show_as_icons_above_regular_list() {
  const tabToPin = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  const initialVisible = getVisibleTabCount();
  await waitForRowCount(tabList, initialVisible);

  // Pin the tab; the pinned-tabs row should appear with one moz-button, and
  // the regular sidebar-tab-list should drop by one.
  gBrowser.pinTab(tabToPin);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () =>
      component.shadowRoot.querySelectorAll(".pinned-tabs moz-button")
        .length === 1
  );
  await waitForRowCount(tabList, initialVisible - 1);

  const pinnedButtons = component.shadowRoot.querySelectorAll(
    ".pinned-tabs moz-button"
  );
  Assert.equal(
    pinnedButtons.length,
    1,
    "One pinned button rendered for the pinned tab."
  );
  Assert.equal(
    pinnedButtons[0].title,
    tabToPin.label,
    "Pinned button title matches the tab title."
  );
  Assert.ok(
    [...tabList.rowEls].every(row => row.tabElement !== tabToPin),
    "Pinned tab is no longer present in the regular tab list."
  );

  // Clicking the pinned button should activate the pinned tab.
  gBrowser.selectedTab = gBrowser.tabs[0];
  const content = SidebarController.browser.contentWindow;
  await content.promiseDocumentFlushed(() => {});
  AccessibilityUtils.setEnv({ focusableRule: false });
  EventUtils.synthesizeMouseAtCenter(pinnedButtons[0], {}, content);
  AccessibilityUtils.resetEnv();

  await TestUtils.waitForCondition(
    () => gBrowser.selectedTab === tabToPin,
    "Clicking the pinned button should activate the corresponding tab."
  );

  // Unpinning should remove the pinned-tabs row and reinsert the tab into the
  // regular sidebar-tab-list.
  gBrowser.unpinTab(tabToPin);
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => !component.shadowRoot.querySelector(".pinned-tabs")
  );
  await waitForRowCount(tabList, initialVisible);

  Assert.ok(
    !component.shadowRoot.querySelector(".pinned-tabs"),
    "Pinned-tabs row is removed when no tabs are pinned."
  );
  Assert.ok(
    [...tabList.rowEls].some(row => row.tabElement === tabToPin),
    "Unpinned tab reappears in the regular tab list."
  );

  BrowserTestUtils.removeTab(tabToPin);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_keyboard_shortcut_toggles_open_tabs_panel() {
  // Ensure sidebar starts closed so the first keystroke is an unambiguous open.
  SidebarTestUtils.closePanel(window);
  Assert.ok(!SidebarController.isOpen, "Sidebar starts closed.");

  // On macOS the shortcut is literal Ctrl+U; on Windows/Linux it is Ctrl+Alt+U.
  const isMac = AppConstants.platform === "macosx";
  const modifiers = isMac ? { ctrlKey: true } : { ctrlKey: true, altKey: true };

  EventUtils.synthesizeKey("u", modifiers);

  await TestUtils.waitForCondition(
    () =>
      SidebarController.isOpen &&
      SidebarController.currentID === "viewOpenTabsSidebar",
    "The Open Tabs sidebar shortcut opens the panel."
  );
  Assert.equal(
    SidebarController.currentID,
    "viewOpenTabsSidebar",
    "Open Tabs panel is the active sidebar."
  );

  // Press the shortcut again — toggle should close the sidebar.
  EventUtils.synthesizeKey("u", modifiers);
  await TestUtils.waitForCondition(
    () => !SidebarController.isOpen,
    "Pressing the shortcut again closes the sidebar."
  );
  Assert.ok(!SidebarController.isOpen, "Sidebar is closed.");
});

add_task(async function test_multiple_windows_render_separate_cards() {
  const component = await showOpenTabsPanel();

  // Wait for the initial card (current window) to render.
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.shadowRoot.querySelectorAll("moz-card").length === 1
  );

  const secondWindow = await BrowserTestUtils.openNewBrowserWindow();

  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.shadowRoot.querySelectorAll("moz-card").length === 2
  );

  let cards = component.shadowRoot.querySelectorAll("moz-card");
  Assert.equal(cards.length, 2, "Two cards rendered for two open windows.");

  // The current window's card should be rendered first.
  Assert.equal(
    cards[0].getAttribute("data-inner-id"),
    String(window.windowGlobalChild.innerWindowId),
    "Current window's card is rendered first."
  );
  Assert.equal(
    cards[1].getAttribute("data-inner-id"),
    String(secondWindow.windowGlobalChild.innerWindowId),
    "Second window's card uses its own inner window id."
  );

  await BrowserTestUtils.closeWindow(secondWindow);

  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.shadowRoot.querySelectorAll("moz-card").length === 1
  );

  Assert.equal(
    component.shadowRoot.querySelectorAll("moz-card").length,
    1,
    "Closing the second window removes its card."
  );

  SidebarTestUtils.closePanel(window);
});

add_task(async function test_nova_current_tab_marker() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });

  const tabA = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );
  const tabB = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:blank"
  );

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  await waitForRowCount(tabList, getVisibleTabCount());

  const rowFor = tab =>
    [...tabList.rowEls].find(rowEl => rowEl.tabElement === tab);

  Assert.ok(
    rowFor(tabB).hasAttribute("current"),
    "The currently-selected tab's row carries the [current] attribute."
  );
  Assert.ok(
    !rowFor(tabA).hasAttribute("current"),
    "A non-selected tab's row does not carry the [current] attribute."
  );

  gBrowser.selectedTab = tabA;
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { subtree: true, attributes: true, attributeFilter: ["current"] },
    () => rowFor(tabA)?.hasAttribute("current")
  );

  Assert.ok(
    rowFor(tabA).hasAttribute("current"),
    "Switching gBrowser.selectedTab moves the [current] attribute."
  );
  Assert.ok(
    !rowFor(tabB).hasAttribute("current"),
    "The previously-selected tab no longer has the [current] attribute."
  );

  BrowserTestUtils.removeTab(tabA);
  BrowserTestUtils.removeTab(tabB);
  SidebarTestUtils.closePanel(window);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_window_card_collapse_state_persists() {
  const PREF = "sidebar.openTabsPanel.collapsedWindows";
  const { SidebarCollapsedWindows } = ChromeUtils.importESModule(
    "moz-src:///browser/components/sidebar/SidebarCollapsedWindows.sys.mjs"
  );

  await SpecialPowers.pushPrefEnv({ set: [[PREF, "{}"]] });

  const component = await showOpenTabsPanel();
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.shadowRoot.querySelector("moz-card")
  );

  const card = component.shadowRoot.querySelector("moz-card");
  Assert.ok(card, "Window card exists.");
  const windowId = card.dataset.windowId;
  Assert.ok(windowId, "Card has a data-window-id from SessionStore.");

  // Collapsing the card writes through to the module and the pref.
  card.dispatchEvent(
    new ToggleEvent("toggle", { newState: "closed", oldState: "open" })
  );
  Assert.ok(
    SidebarCollapsedWindows.isCollapsed(window),
    "Module reports the current window as collapsed."
  );
  Assert.equal(
    JSON.parse(Services.prefs.getStringPref(PREF))[windowId],
    true,
    "Pref persists the collapsed state for this window."
  );

  // Hide and re-show the panel; the module's state should survive.
  SidebarTestUtils.closePanel(window);
  const component2 = await showOpenTabsPanel();
  await BrowserTestUtils.waitForMutationCondition(
    component2.shadowRoot,
    { childList: true, subtree: true },
    () => component2.shadowRoot.querySelector("moz-card")
  );
  Assert.ok(
    SidebarCollapsedWindows.isCollapsed(window),
    "Module still reports collapsed after panel re-mount."
  );

  // Expanding clears the entry from both the module and the pref.
  const card2 = component2.shadowRoot.querySelector("moz-card");
  card2.dispatchEvent(
    new ToggleEvent("toggle", { newState: "open", oldState: "closed" })
  );
  Assert.equal(
    SidebarCollapsedWindows.isCollapsed(window),
    false,
    "Expanding clears the module's state."
  );
  Assert.equal(
    JSON.parse(Services.prefs.getStringPref(PREF))[windowId],
    undefined,
    "Pref no longer has an entry for this window."
  );

  SidebarTestUtils.closePanel(window);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_private_window_skips_pref() {
  const PREF = "sidebar.openTabsPanel.collapsedWindows";
  const { SidebarCollapsedWindows } = ChromeUtils.importESModule(
    "moz-src:///browser/components/sidebar/SidebarCollapsedWindows.sys.mjs"
  );

  await SpecialPowers.pushPrefEnv({ set: [[PREF, "{}"]] });

  const privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });

  // The module must never write the pref for a private window, regardless
  // of how the toggle path is exercised.
  SidebarCollapsedWindows.collapseWindow(privateWin);
  Assert.equal(
    Services.prefs.getStringPref(PREF),
    "{}",
    "Pref is untouched after collapseWindow on a private window."
  );
  Assert.equal(
    SidebarCollapsedWindows.isCollapsed(privateWin),
    false,
    "isCollapsed reports false for private windows."
  );

  await BrowserTestUtils.closeWindow(privateWin);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_window_close_drops_pref_entry() {
  const PREF = "sidebar.openTabsPanel.collapsedWindows";
  const { SidebarCollapsedWindows } = ChromeUtils.importESModule(
    "moz-src:///browser/components/sidebar/SidebarCollapsedWindows.sys.mjs"
  );

  await SpecialPowers.pushPrefEnv({ set: [[PREF, "{}"]] });

  const secondWin = await BrowserTestUtils.openNewBrowserWindow();
  const secondWindowId = SessionStore.getWindowId(secondWin);
  Assert.ok(secondWindowId, "Second window has a SessionStore id.");

  // Collapse the second window via the module and confirm the pref records it.
  SidebarCollapsedWindows.collapseWindow(secondWin);
  Assert.ok(
    SidebarCollapsedWindows.isCollapsed(secondWin),
    "Module reports the second window as collapsed."
  );
  Assert.equal(
    JSON.parse(Services.prefs.getStringPref(PREF))[secondWindowId],
    true,
    "Pref records the collapsed entry for the second window."
  );

  // Close the window. The module's domwindowclosed observer should drop the
  // entry from both the in-memory map and the pref.
  await BrowserTestUtils.closeWindow(secondWin);

  await TestUtils.waitForCondition(
    () =>
      JSON.parse(Services.prefs.getStringPref(PREF))[secondWindowId] ===
      undefined,
    "Pref entry for the closed window is dropped."
  );

  await SpecialPowers.popPrefEnv();
});

add_task(async function test_search_filters_visible_rows() {
  const appleTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,<title>Apple</title>"
  );
  const bananaTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,<title>Banana</title>"
  );

  const component = await showOpenTabsPanel();
  const allTabsCount = getVisibleTabCount();
  await waitForRowCount(getTabList(component), allTabsCount);

  const searchBox = component.shadowRoot.querySelector("moz-input-search");
  Assert.ok(searchBox, "Search input is present in the panel.");

  // Searching shows the results header and filters the window card rows.
  searchBox.dispatchEvent(
    new CustomEvent("MozInputSearch:search", { detail: { query: "Apple" } })
  );
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () =>
      component.shadowRoot.querySelector(
        "[data-l10n-id='sidebar-search-results-header']"
      )
  );
  await waitForRowCount(getTabList(component), 1);
  Assert.ok(
    getTabList(component).rowEls[0].url.includes("Apple"),
    "Only the matching tab is shown while searching."
  );
  const count = component.shadowRoot.querySelector("[slot='secondary-header']");
  Assert.equal(
    JSON.parse(count.getAttribute("data-l10n-args")).count,
    1,
    "The results count reflects the single match."
  );

  // Clearing removes the header and restores all rows.
  searchBox.dispatchEvent(
    new CustomEvent("MozInputSearch:search", { detail: { query: "" } })
  );
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () =>
      !component.shadowRoot.querySelector(
        "[data-l10n-id='sidebar-search-results-header']"
      )
  );
  await waitForRowCount(getTabList(component), allTabsCount);
  Assert.equal(
    getTabList(component).rowEls.length,
    allTabsCount,
    "Clearing the query restores all tab rows."
  );

  BrowserTestUtils.removeTab(appleTab);
  BrowserTestUtils.removeTab(bananaTab);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_medium_view_shows_domain_and_time() {
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "https://example.com/"
  );

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  await waitForRowCount(tabList, getVisibleTabCount());

  const row = [...tabList.rowEls].find(r => r.url === "https://example.com/");
  Assert.ok(row, "Found the row for the opened tab.");

  const domain = row.domainEl;
  Assert.ok(domain, "The row renders a domain in the medium view.");
  Assert.equal(
    domain.textContent.trim(),
    "example.com",
    "The domain shows the base domain of the tab URL."
  );

  const time = row.timeEl;
  Assert.ok(time, "The row renders a time in the medium view.");
  Assert.equal(
    time.getAttribute("data-l10n-id"),
    "fxviewtabrow-time",
    "The time uses the clock-time format."
  );

  // about: pages have no base domain, so the row falls back to the formatted
  // URI instead of showing nothing (matching Firefox View).
  const aboutTab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:robots"
  );
  await waitForRowCount(tabList, getVisibleTabCount());
  const aboutRow = [...tabList.rowEls].find(r => r.url === "about:robots");
  Assert.ok(aboutRow, "Found the row for the about: tab.");
  Assert.equal(
    aboutRow.domainEl.textContent.trim(),
    "about:robots",
    "An about: page shows its formatted URI, not an empty domain."
  );
  BrowserTestUtils.removeTab(aboutTab);
  await waitForRowCount(tabList, getVisibleTabCount());

  // The domain and time are revealed by container-query breakpoints on the
  // row width, so constrain the list width to exercise each state.
  tabList.style.width = "240px";
  await TestUtils.waitForCondition(
    () => !BrowserTestUtils.isVisible(domain),
    "Domain is hidden when the panel is narrow."
  );
  Assert.ok(
    !BrowserTestUtils.isVisible(time),
    "Time is hidden when the panel is narrow."
  );

  tabList.style.width = "370px";
  await TestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(domain),
    "Domain becomes visible past the first breakpoint."
  );
  Assert.ok(
    !BrowserTestUtils.isVisible(time),
    "Time is still hidden before the second breakpoint."
  );

  tabList.style.width = "520px";
  await TestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(time),
    "Time becomes visible past the second breakpoint."
  );
  Assert.ok(
    BrowserTestUtils.isVisible(domain),
    "Domain remains visible when the panel is wide."
  );

  tabList.style.width = "";

  BrowserTestUtils.removeTab(tab);
  SidebarTestUtils.closePanel(window);
});

add_task(async function test_pinned_tab_selected_marker() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });

  const nonPinnedTab = gBrowser.selectedTab;
  const tabA = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,<title>PinnedA</title>"
  );
  const tabB = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "data:text/html,<title>PinnedB</title>"
  );
  gBrowser.pinTab(tabA);
  gBrowser.pinTab(tabB);

  const component = await showOpenTabsPanel();
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () =>
      component.shadowRoot.querySelectorAll(".pinned-tabs moz-button")
        .length === 2
  );

  const buttonFor = title =>
    [...component.shadowRoot.querySelectorAll(".pinned-tabs moz-button")].find(
      button => button.title === title
    );

  // tabB is the currently-selected tab, so its pinned button is marked.
  await TestUtils.waitForCondition(
    () => buttonFor("PinnedB").classList.contains("selected"),
    "The selected pinned tab's button carries the selected class."
  );
  Assert.ok(
    !buttonFor("PinnedA").classList.contains("selected"),
    "A non-selected pinned tab's button does not carry the selected class."
  );

  // Selecting the other pinned tab moves the marker.
  gBrowser.selectedTab = tabA;
  await TestUtils.waitForCondition(
    () => buttonFor("PinnedA").classList.contains("selected"),
    "Selecting the other pinned tab moves the selected class."
  );
  Assert.ok(
    !buttonFor("PinnedB").classList.contains("selected"),
    "The previously-selected pinned tab no longer carries the selected class."
  );

  // Selecting a non-pinned tab clears the marker from both pinned buttons.
  gBrowser.selectedTab = nonPinnedTab;
  await TestUtils.waitForCondition(
    () =>
      !buttonFor("PinnedA").classList.contains("selected") &&
      !buttonFor("PinnedB").classList.contains("selected"),
    "Selecting a non-pinned tab clears the marker from the pinned buttons."
  );

  gBrowser.unpinTab(tabA);
  gBrowser.unpinTab(tabB);
  BrowserTestUtils.removeTab(tabA);
  BrowserTestUtils.removeTab(tabB);
  SidebarTestUtils.closePanel(window);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_inactive_window_deemphasizes_selected_border() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.nova.enabled", true]],
  });

  const component = await showOpenTabsPanel();
  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.shadowRoot.querySelectorAll("moz-card").length === 1
  );

  const secondWindow = await BrowserTestUtils.openNewBrowserWindow();
  await BrowserTestUtils.openNewForegroundTab(
    secondWindow.gBrowser,
    "data:text/html,<title>Inactive</title>"
  );
  const pinnedTab = secondWindow.gBrowser.tabs[0];
  secondWindow.gBrowser.pinTab(pinnedTab);
  secondWindow.gBrowser.selectedTab = pinnedTab;

  await BrowserTestUtils.waitForMutationCondition(
    component.shadowRoot,
    { childList: true, subtree: true },
    () => component.shadowRoot.querySelectorAll("moz-card").length === 2
  );

  const cards = component.shadowRoot.querySelectorAll("moz-card");
  const currentList = cards[0].querySelector("sidebar-tab-list");
  const inactiveList = cards[1].querySelector("sidebar-tab-list");

  await TestUtils.waitForCondition(
    () => inactiveList.hasAttribute("inactive-window"),
    "The non-current window's tab list is marked inactive."
  );
  Assert.ok(
    !currentList.hasAttribute("inactive-window"),
    "The current window's tab list is not marked inactive."
  );

  await BrowserTestUtils.waitForMutationCondition(
    inactiveList.shadowRoot,
    { childList: true, subtree: true, attributes: true },
    () =>
      !![...inactiveList.rowEls].length &&
      [...inactiveList.rowEls].every(row => row.hasAttribute("inactive-window"))
  );
  Assert.ok(
    [...currentList.rowEls].every(row => !row.hasAttribute("inactive-window")),
    "Rows in the current window are not marked inactive."
  );

  const inactivePinnedButton = await TestUtils.waitForCondition(() =>
    cards[1].querySelector(".pinned-tabs moz-button.selected")
  );
  Assert.ok(
    inactivePinnedButton.classList.contains("inactive"),
    "The selected pinned tab in a non-current window is marked inactive."
  );

  await BrowserTestUtils.closeWindow(secondWindow);
  SidebarTestUtils.closePanel(window);
  await SpecialPowers.popPrefEnv();
});

add_task(async function test_context_menu_close_tab() {
  const url = "data:text/html,CloseMe";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  await waitForRowCount(tabList, getVisibleTabCount());

  const row = [...tabList.rowEls].find(r => r.url === url);
  Assert.ok(row, "Found the row for the opened tab.");

  await activateContextMenuItem(
    row.mainEl,
    "sidebar-opentabs-context-close-tab"
  );
  await BrowserTestUtils.waitForMutationCondition(
    gBrowser.tabContainer,
    { childList: true, subtree: true },
    () => !gBrowser.tabs.includes(tab)
  );
  Assert.ok(!gBrowser.tabs.includes(tab), "The context menu closed the tab.");

  SidebarTestUtils.closePanel(window);
});

add_task(async function test_context_menu_copy_link() {
  const url = "data:text/html,CopyMe";
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, url);

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  await waitForRowCount(tabList, getVisibleTabCount());

  const row = [...tabList.rowEls].find(r => r.url === url);
  Assert.ok(row, "Found the row for the opened tab.");

  await activateContextMenuItem(
    row.mainEl,
    "sidebar-opentabs-context-copy-link"
  );
  await TestUtils.waitForCondition(
    () => SpecialPowers.getClipboardData("text/plain") === url,
    "The context menu copied the tab's URL to the clipboard."
  );

  BrowserTestUtils.removeTab(tab);
  SidebarTestUtils.closePanel(window);
});

async function selectSortOption(component, sortItemProp) {
  const content = SidebarController.browser.contentWindow;
  const menu = component._menu;
  const menuShown = BrowserTestUtils.waitForEvent(menu, "popupshown");
  EventUtils.synthesizeMouseAtCenter(component.menuButton, {}, content);
  await menuShown;

  const menuHidden = BrowserTestUtils.waitForEvent(menu, "popuphidden");
  menu.activateItem(component[sortItemProp]);
  await menuHidden;
  await component.updateComplete;
}

add_task(async function test_opentabs_sort_menu() {
  const urlB = "data:text/html,sortb";
  const urlA = "data:text/html,sorta";
  // Open B first, then A, so tab-strip order is [B, A] and A is most recent.
  const tabB = await BrowserTestUtils.openNewForegroundTab(gBrowser, urlB);
  const tabA = await BrowserTestUtils.openNewForegroundTab(gBrowser, urlA);

  const component = await showOpenTabsPanel();
  const tabList = getTabList(component);
  await waitForRowCount(tabList, getVisibleTabCount());

  const rowUrls = () => [...tabList.rowEls].map(row => row.url);
  const orderedByUrl = (first, second) => {
    const urls = rowUrls();
    const a = urls.indexOf(first);
    const b = urls.indexOf(second);
    return a !== -1 && b !== -1 && a < b;
  };

  Assert.equal(
    component.sortOption,
    "tabStripOrder",
    "The panel defaults to tab-strip order."
  );
  Assert.ok(
    orderedByUrl(urlB, urlA),
    "Tab order lists the earlier-opened tab first."
  );

  info("Sort by most recent.");
  await selectSortOption(component, "_menuSortByRecency");
  Assert.equal(component.sortOption, "recency", "Sort option is recency.");
  Assert.equal(
    Services.prefs.getStringPref("sidebar.openTabsPanel.sortOption"),
    "recency",
    "The recency choice is persisted to the pref."
  );
  Assert.ok(
    component._menuSortByRecency.hasAttribute("checked"),
    "The Most recent menu item is checked."
  );
  Assert.ok(
    !component._menuSortByOrder.hasAttribute("checked"),
    "The Tab order menu item is not checked."
  );
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => orderedByUrl(urlA, urlB)
  );

  info("Sort by tab order.");
  await selectSortOption(component, "_menuSortByOrder");
  Assert.equal(
    component.sortOption,
    "tabStripOrder",
    "Sort option is tab-strip order."
  );
  Assert.equal(
    Services.prefs.getStringPref("sidebar.openTabsPanel.sortOption"),
    "tabStripOrder",
    "The tab-order choice is persisted to the pref."
  );
  Assert.ok(
    component._menuSortByOrder.hasAttribute("checked"),
    "The Tab order menu item is checked."
  );
  await BrowserTestUtils.waitForMutationCondition(
    tabList.shadowRoot,
    { childList: true, subtree: true },
    () => orderedByUrl(urlB, urlA)
  );

  BrowserTestUtils.removeTab(tabA);
  BrowserTestUtils.removeTab(tabB);
  SidebarTestUtils.closePanel(window);
  Services.prefs.clearUserPref("sidebar.openTabsPanel.sortOption");
});

add_task(async function test_opentabs_sort_persists() {
  let component = await showOpenTabsPanel();

  info("Choose most recent, then close the sidebar.");
  await selectSortOption(component, "_menuSortByRecency");
  await SidebarController.waitUntilStable();
  SidebarController.hide();

  info("Reopen the panel and verify the sort option was restored.");
  component = await showOpenTabsPanel();
  const recencyItem = component._menuSortByRecency;
  await BrowserTestUtils.waitForMutationCondition(
    recencyItem,
    { attributes: true, attributeFilter: ["checked"] },
    () => recencyItem.hasAttribute("checked")
  );
  Assert.equal(
    component.sortOption,
    "recency",
    "The panel restored the persisted sort option on open."
  );

  SidebarController.hide();
  Services.prefs.clearUserPref("sidebar.openTabsPanel.sortOption");
});
