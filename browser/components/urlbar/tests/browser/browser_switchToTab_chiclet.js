/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Test for chiclet upon switching tab mode.
 */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  ContextualIdentityService:
    "resource://gre/modules/ContextualIdentityService.sys.mjs",
  TabGroupTestUtils: "resource://testing-common/TabGroupTestUtils.sys.mjs",
});

const TEST_URL = `${TEST_BASE_URL}dummy_page.html`;

add_task(async function test_with_oneoff_button() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.urlbar.scotchBonnet.enableOverride", false],
      // Tests get no default Top Sites, and the view is opened on an empty
      // string here, which would otherwise leave it closed with no results.
      [
        "browser.newtabpage.activity-stream.default.sites",
        "https://example.com/",
      ],
    ],
  });
  info("Loading test page into first tab");
  let promiseLoad = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    TEST_URL
  );
  BrowserTestUtils.startLoadingURIString(gBrowser.selectedBrowser, TEST_URL);
  await promiseLoad;

  info("Opening a new tab");
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  info("Wait for autocomplete");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "",
  });

  info("Enter Tabs mode");
  await UrlbarTestUtils.enterSearchMode(window, {
    source: UrlbarUtils.RESULT_SOURCE.TABS,
  });

  info("Select first popup entry");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "dummy",
  });
  EventUtils.synthesizeKey("KEY_ArrowDown");
  const result = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    UrlbarTestUtils.getSelectedRowIndex(window)
  );
  Assert.equal(result.type, UrlbarUtils.RESULT_TYPE.TAB_SWITCH);

  info("Enter escape key");
  EventUtils.synthesizeKey("KEY_Escape");

  info("Check label visibility");
  const searchModeTitle = document.getElementById(
    "urlbar-search-mode-indicator-title"
  );
  const switchTabLabel = document.getElementById("urlbar-label-switchtab");
  await BrowserTestUtils.waitForCondition(
    () =>
      BrowserTestUtils.isVisible(searchModeTitle) &&
      searchModeTitle.textContent === "Tabs",
    "Waiting until the search mode title will be visible"
  );
  await BrowserTestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(switchTabLabel),
    "Waiting until the switch tab label will be visible"
  );

  await PlacesUtils.history.clear();
  gBrowser.removeTab(tab);
});

add_task(async function test_with_keytype() {
  info("Loading test page into first tab");
  let promiseLoad = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    false,
    TEST_URL
  );
  BrowserTestUtils.startLoadingURIString(gBrowser, TEST_URL);
  await promiseLoad;

  info("Opening a new tab");
  const tab = await BrowserTestUtils.openNewForegroundTab(gBrowser);

  info("Enter Tabs mode with keytype");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "%",
  });

  info("Select second popup entry");
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "dummy",
  });
  EventUtils.synthesizeKey("KEY_ArrowDown");
  const result = await UrlbarTestUtils.getDetailsOfResultAt(
    window,
    UrlbarTestUtils.getSelectedRowIndex(window)
  );
  Assert.equal(result.type, UrlbarUtils.RESULT_TYPE.TAB_SWITCH);

  info("Enter escape key");
  EventUtils.synthesizeKey("KEY_Escape");

  info("Check label visibility");
  const searchModeTitle = document.getElementById(
    "urlbar-search-mode-indicator-title"
  );
  const switchTabLabel = document.getElementById("urlbar-label-switchtab");
  await BrowserTestUtils.waitForCondition(
    () => BrowserTestUtils.isHidden(searchModeTitle),
    "Waiting until the search mode title will be hidden"
  );
  await BrowserTestUtils.waitForCondition(
    () => BrowserTestUtils.isVisible(switchTabLabel),
    "Waiting until the switch tab label will be visible"
  );

  await PlacesUtils.history.clear();
  gBrowser.removeTab(tab);
});

add_task(async function test_chiclet_contextual_id() {
  // Add a container tab in the background. Use `openNewForegroundTab` since it
  // waits for the page to load, which is important for triggering the
  // switch-to-tab, and then switch back to the first tab.
  let containerTab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: () => {
      gBrowser.selectedTab = BrowserTestUtils.addTab(
        gBrowser,
        "https://example.com/test_chiclet_contextual_id",
        { userContextId: 1 }
      );
    },
  });
  await BrowserTestUtils.switchTab(gBrowser, gBrowser.tabs[0]);

  // Do a search that matches the tab. There should be a switch-to-tab result.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test_chiclet_contextual_id",
  });

  let {
    result,
    element: { row },
  } = await getDetailsOfTabSwitchResult();

  Assert.equal(
    result.type,
    UrlbarUtils.RESULT_TYPE.TAB_SWITCH,
    "Result at index 1 should be a tab switch"
  );

  // The row should have the contextual ID chiclet, and the chiclet shouldn't
  // have any l10n attributes.
  let chiclet = row.querySelector(".action-contextualidentity");
  Assert.ok(chiclet, "The contextual ID chiclet should be in the row");
  Assert.ok(
    BrowserTestUtils.isVisible(chiclet),
    "The contextual ID chiclet should be visible"
  );
  Assert.deepEqual(
    document.l10n.getAttributes(chiclet),
    { id: null, args: null },
    "The contextual ID chiclet should not have l10n attributes"
  );

  // Check its text content.
  let label = ContextualIdentityService.getUserContextLabel(1);
  Assert.ok(label, "Sanity check: A label is defined for the contextual ID");
  Assert.equal(
    chiclet.textContent,
    label,
    "The contextual ID chiclet should have the expected label"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await BrowserTestUtils.removeTab(containerTab);
});

add_task(async function test_chiclet_tab_group() {
  // Add a tab group in the background. Use `openNewForegroundTab` since it
  // waits for the page to load, which is important for triggering the
  // switch-to-tab, and then switch back to the first tab.
  let label = "test_chiclet_tab_group";
  let tabs = [
    await BrowserTestUtils.openNewForegroundTab({
      gBrowser,
      url: "https://example.com/" + label,
    }),
  ];
  let tabGroup = gBrowser.addTabGroup(tabs, {
    label,
    color: "blue",
  });
  await BrowserTestUtils.switchTab(gBrowser, gBrowser.tabs[0]);

  // Do a search that matches a tab in the group. There should be a
  // switch-to-tab result.
  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: label,
  });

  let {
    result,
    element: { row },
  } = await getDetailsOfTabSwitchResult();

  Assert.equal(
    result.type,
    UrlbarUtils.RESULT_TYPE.TAB_SWITCH,
    "Result at index 1 should be a tab switch"
  );

  // The row should have the tab group chiclet, and the chiclet shouldn't have
  // any l10n attributes.
  let chiclet = row.querySelector(".urlbarView-tabGroup");
  Assert.ok(chiclet, "The tab group chiclet should be in the row");
  Assert.ok(
    BrowserTestUtils.isVisible(chiclet),
    "The tab group chiclet should be visible"
  );
  Assert.deepEqual(
    document.l10n.getAttributes(chiclet),
    { id: null, args: null },
    "The tab group chiclet should not have l10n attributes"
  );

  // Check the chiclet's children, the full-width and narrow-width labels.
  let fullLabel = chiclet.children[0];
  Assert.ok(fullLabel, "The tab group full-width label should exist");
  Assert.equal(
    fullLabel.textContent,
    label,
    "The tab group full-width label should be the full label text"
  );

  let narrowLabel = chiclet.children[1];
  Assert.ok(narrowLabel, "The tab group narrow-width label should exist");
  Assert.equal(
    narrowLabel.textContent,
    label[0],
    "The tab group narrow-width label should be first char of the full label"
  );

  await UrlbarTestUtils.promisePopupClose(window);
  await TabGroupTestUtils.removeTabGroup(tabGroup);
  TabGroupTestUtils.forgetSavedTabGroups();
});

add_task(async function test_chiclet_tab_group_no_stale_after_row_reuse() {
  const TAB_URL =
    "https://example.com/test_chiclet_tab_group_no_stale_switch_tab";
  const HISTORY_URL =
    "https://example.com/test_chiclet_tab_group_no_stale_history";

  await PlacesTestUtils.addVisits(HISTORY_URL);

  let tab = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    url: TAB_URL,
  });
  let tabGroup = gBrowser.addTabGroup([tab], {
    label: "stale_chiclet_test",
    color: "blue",
  });
  await BrowserTestUtils.switchTab(gBrowser, gBrowser.tabs[0]);

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test_chiclet_tab_group_no_stale_switch_tab",
  });

  let switchTabDetails = await getDetailsOfTabSwitchResult();
  Assert.ok(
    switchTabDetails?.element.row.querySelector(".urlbarView-tabGroup"),
    "Tab group chiclet should be present on the TAB_SWITCH row"
  );

  await UrlbarTestUtils.promiseAutocompleteResultPopup({
    window,
    value: "test_chiclet_tab_group_no_stale_history",
  });

  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    Assert.notEqual(
      details.type,
      UrlbarUtils.RESULT_TYPE.TAB_SWITCH,
      `Row ${i} should not be a TAB_SWITCH result`
    );
    Assert.ok(
      !details.element.row.querySelector(".urlbarView-tabGroup"),
      `Row ${i} should not have a stale tab group chiclet`
    );
  }

  await UrlbarTestUtils.promisePopupClose(window);
  await TabGroupTestUtils.removeTabGroup(tabGroup);
  TabGroupTestUtils.forgetSavedTabGroups();
  await PlacesUtils.history.clear();
});

async function getDetailsOfTabSwitchResult() {
  for (let i = 0; i < UrlbarTestUtils.getResultCount(window); i++) {
    let details = await UrlbarTestUtils.getDetailsOfResultAt(window, i);
    if (details.type == UrlbarUtils.RESULT_TYPE.TAB_SWITCH) {
      return details;
    }
  }
  return null;
}
