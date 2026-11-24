/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from head_smartformfill_autocomplete.js */
Services.scriptloader.loadSubScript(
  getRootDirectory(gTestPath) + "head_smartformfill_autocomplete.js",
  this
);

const { MAX_SELECTED_TABS } = ChromeUtils.importESModule(
  "chrome://browser/content/aiwindow/modules/SmartFormFillConstants.mjs"
);
const { NonPrivateTabs } = ChromeUtils.importESModule(
  "resource:///modules/OpenTabs.sys.mjs"
);

const OTHER_SOURCE_URL = "https://example.net/";

function waitForTabChange(sourceEvent) {
  return BrowserTestUtils.waitForEvent(
    NonPrivateTabs,
    "TabChange",
    false,
    event => event.detail.sourceEvents.includes(sourceEvent)
  );
}

async function getSmartFormFillRow(popup) {
  let item;
  await TestUtils.waitForCondition(
    () => (item = popup.querySelector('[originaltype="smartFormFill"]')),
    "Waiting for the Smart Form Fill autocomplete row"
  );

  const row = item.querySelector("autocomplete-row-item");
  await row.updateComplete;
  return row;
}

async function openTabSelector(win, browser, row) {
  const dialogManager = win.gBrowser
    .getTabDialogBox(browser)
    .getTabDialogManager();
  const editButton = row.renderRoot.querySelector(
    "moz-button.secondary-action"
  );

  Assert.ok(editButton, "The edit-sources button should be available");

  const item = row.closest("richlistitem");
  Assert.ok(item, "The Smart Form Fill autocomplete item should be available");

  EventUtils.synthesizeMouseAtCenter(row, { type: "mousemove" }, win);
  await TestUtils.waitForCondition(
    () => item.hasAttribute("selected"),
    "Waiting for the Smart Form Fill row to be pointer-selected"
  );
  EventUtils.synthesizeMouseAtCenter(editButton.buttonEl, {}, win);

  await TestUtils.waitForCondition(
    () => dialogManager._dialogs.length === 1,
    "Waiting for the tab-selector dialog"
  );

  const dialog = dialogManager._dialogs[0];
  await dialog._dialogReady;

  const dialogWindow = dialog._frame.contentWindow;
  const tabSelector = dialogWindow.document.querySelector(
    "ai-sff-tab-selector"
  );

  Assert.ok(tabSelector, "The tab-selector component should be rendered");
  await tabSelector.updateComplete;

  return { dialogManager, dialogWindow, tabSelector };
}

function getDialogTab(tabSelector, url) {
  const groups = tabSelector.renderRoot.querySelectorAll("moz-box-group");
  const tabLists = [tabSelector.suggestedTabs, tabSelector.otherTabs];

  for (let listIndex = 0; listIndex < tabLists.length; listIndex++) {
    const tabIndex = tabLists[listIndex].findIndex(tab => tab.url === url);
    if (tabIndex !== -1) {
      return {
        tab: tabLists[listIndex][tabIndex],
        toggle: groups[listIndex].querySelectorAll("moz-toggle")[tabIndex],
      };
    }
  }

  throw new Error(`Could not find dialog tab for ${url}`);
}

async function toggleTab(tabSelector, url) {
  const { tab, toggle } = getDialogTab(tabSelector, url);
  const previousPressed = tab.pressed;

  await toggle.updateComplete;
  Assert.ok(!toggle.disabled, `The toggle for ${url} should be enabled`);

  const toggled = BrowserTestUtils.waitForEvent(toggle, "toggle");
  toggle.buttonEl.click();
  await toggled;
  await tabSelector.updateComplete;

  Assert.notEqual(
    getDialogTab(tabSelector, url).tab.pressed,
    previousPressed,
    `The pressed state for ${url} should change`
  );
}

function getDialogButton(tabSelector, l10nId) {
  return tabSelector.renderRoot.querySelector(
    `moz-button[data-l10n-id="${l10nId}"]`
  );
}

function activateDialogButton(tabSelector, l10nId, dialogWindow) {
  const button = getDialogButton(tabSelector, l10nId);
  Assert.ok(button, `The ${l10nId} button should be rendered`);
  EventUtils.synthesizeMouseAtCenter(button.buttonEl, {}, dialogWindow);
}

function getSourceLabels(row) {
  return [...row.renderRoot.querySelectorAll(".source-label")].map(label =>
    label.textContent.trim()
  );
}

async function closeDialogAndWaitForAutocomplete(
  dialogManager,
  popup,
  closeDialog
) {
  const popupShown = BrowserTestUtils.waitForPopupEvent(popup, "shown");
  closeDialog();

  await TestUtils.waitForCondition(
    () => !dialogManager._dialogs.length,
    "Waiting for the tab-selector dialog to close"
  );
  await popupShown;

  return getSmartFormFillRow(popup);
}

function activateDialogButtonAndWaitForAutocomplete(
  dialogManager,
  popup,
  tabSelector,
  l10nId,
  dialogWindow
) {
  return closeDialogAndWaitForAutocomplete(dialogManager, popup, () =>
    activateDialogButton(tabSelector, l10nId, dialogWindow)
  );
}

async function openForm(win) {
  const tabUpdated = waitForTabChange("TabAttrModified");
  await BrowserTestUtils.openNewForegroundTab(win.gBrowser, FORM_URL);
  await tabUpdated;
}

describe("Smart Form Fill tab selector", () => {
  let mockEngineManager;
  let originalRegion;
  let otherTab;
  let sourceTab;
  let win;

  beforeEach(async () => {
    ({ win, mockEngineManager, originalRegion } =
      await setupSmartFormFillAutocompleteTest());

    const sourceTabUpdated = waitForTabChange("TabAttrModified");
    sourceTab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      SOURCE_URL
    );
    await sourceTabUpdated;

    const otherTabUpdated = waitForTabChange("TabAttrModified");
    otherTab = await BrowserTestUtils.openNewForegroundTab(
      win.gBrowser,
      OTHER_SOURCE_URL
    );
    await otherTabUpdated;
  });

  afterEach(async () => {
    await cleanupSmartFormFillAutocompleteTest({
      win,
      mockEngineManager,
      originalRegion,
    });

    mockEngineManager = null;
    win = null;
  });

  it("renders the initial tab selection", async () => {
    await openForm(win);

    const { browser, popup, row } = await openAutocomplete(
      win,
      "#email",
      mockEngineManager,
      { selectedSourceUrl: SOURCE_URL }
    );
    const { dialogManager, dialogWindow, tabSelector } = await openTabSelector(
      win,
      browser,
      row
    );

    Assert.deepEqual(
      tabSelector.suggestedTabs.map(tab => tab.url),
      [SOURCE_URL],
      "The relevant source should be shown as suggested"
    );
    Assert.ok(
      getDialogTab(tabSelector, SOURCE_URL).toggle.pressed,
      "The suggested tab should initially be selected"
    );
    Assert.ok(
      tabSelector.otherTabs.some(tab => tab.url === OTHER_SOURCE_URL),
      "The other open tab should be shown"
    );
    Assert.ok(
      !getDialogTab(tabSelector, OTHER_SOURCE_URL).toggle.pressed,
      "The other tab should initially be unselected"
    );
    Assert.ok(
      ![...tabSelector.suggestedTabs, ...tabSelector.otherTabs].some(
        tab => tab.url === FORM_URL
      ),
      "The form tab should not be shown"
    );

    await activateDialogButtonAndWaitForAutocomplete(
      dialogManager,
      popup,
      tabSelector,
      "ai-smart-form-fill-cancel-tab-select-button",
      dialogWindow
    );
    await closeAutocomplete(browser);
  });

  it("disables Done when no tabs are selected", async () => {
    await openForm(win);

    const { browser, popup, row } = await openAutocomplete(
      win,
      "#email",
      mockEngineManager,
      { selectedSourceUrl: SOURCE_URL }
    );
    const { dialogManager, dialogWindow, tabSelector } = await openTabSelector(
      win,
      browser,
      row
    );

    await toggleTab(tabSelector, SOURCE_URL, dialogWindow);

    Assert.ok(
      getDialogButton(
        tabSelector,
        "ai-smart-form-fill-accept-tab-select-button"
      ).disabled,
      "Done should be disabled"
    );

    await activateDialogButtonAndWaitForAutocomplete(
      dialogManager,
      popup,
      tabSelector,
      "ai-smart-form-fill-cancel-tab-select-button",
      dialogWindow
    );
    await closeAutocomplete(browser);
  });

  it("saves source changes when Done is pressed", async () => {
    await openForm(win);

    const { browser, popup, row } = await openAutocomplete(
      win,
      "#email",
      mockEngineManager,
      { selectedSourceUrl: SOURCE_URL }
    );
    const { dialogManager, dialogWindow, tabSelector } = await openTabSelector(
      win,
      browser,
      row
    );

    await toggleTab(tabSelector, SOURCE_URL, dialogWindow);
    await toggleTab(tabSelector, OTHER_SOURCE_URL, dialogWindow);
    const restoredRow = await activateDialogButtonAndWaitForAutocomplete(
      dialogManager,
      popup,
      tabSelector,
      "ai-smart-form-fill-accept-tab-select-button",
      dialogWindow
    );

    const sourceLabels = getSourceLabels(restoredRow);
    Assert.equal(
      sourceLabels.length,
      1,
      "The reopened row should show one saved source"
    );
    Assert.stringContains(
      sourceLabels[0],
      otherTab.label,
      "The reopened row should show the saved source"
    );

    await closeAutocomplete(browser);
  });

  it("enforces the five-tab selection limit", async () => {
    const otherURLs = [OTHER_SOURCE_URL];
    for (let index = 1; index < MAX_SELECTED_TABS; index++) {
      const url = `${OTHER_SOURCE_URL}?source=${index}`;
      const tabUpdated = waitForTabChange("TabAttrModified");
      otherURLs.push(url);
      await BrowserTestUtils.openNewForegroundTab(win.gBrowser, url);
      await tabUpdated;
    }

    await openForm(win);

    const { browser, popup, row } = await openAutocomplete(
      win,
      "#email",
      mockEngineManager,
      { selectedSourceUrl: SOURCE_URL }
    );
    const { dialogManager, dialogWindow, tabSelector } = await openTabSelector(
      win,
      browser,
      row
    );

    for (let index = 0; index < MAX_SELECTED_TABS - 1; index++) {
      await toggleTab(tabSelector, otherURLs[index]);
    }

    const lastOtherURL = otherURLs.at(-1);
    Assert.ok(
      getDialogTab(tabSelector, lastOtherURL).toggle.disabled,
      "An unselected tab should be disabled at the limit"
    );

    await toggleTab(tabSelector, SOURCE_URL);
    Assert.ok(
      !getDialogTab(tabSelector, lastOtherURL).toggle.disabled,
      "An unselected tab should be enabled after another is deselected"
    );

    await activateDialogButtonAndWaitForAutocomplete(
      dialogManager,
      popup,
      tabSelector,
      "ai-smart-form-fill-cancel-tab-select-button",
      dialogWindow
    );
    await closeAutocomplete(browser);
  });

  it("preserves source selection when Cancel is pressed", async () => {
    await openForm(win);

    const { browser, popup, row } = await openAutocomplete(
      win,
      "#email",
      mockEngineManager,
      { selectedSourceUrl: SOURCE_URL }
    );
    const { dialogManager, dialogWindow, tabSelector } = await openTabSelector(
      win,
      browser,
      row
    );

    await toggleTab(tabSelector, SOURCE_URL);
    await toggleTab(tabSelector, OTHER_SOURCE_URL);
    const restoredRow = await activateDialogButtonAndWaitForAutocomplete(
      dialogManager,
      popup,
      tabSelector,
      "ai-smart-form-fill-cancel-tab-select-button",
      dialogWindow
    );

    const sourceLabels = getSourceLabels(restoredRow);
    Assert.equal(
      sourceLabels.length,
      1,
      "The reopened row should show one source"
    );
    Assert.stringContains(
      sourceLabels[0],
      sourceTab.label,
      "The reopened row should retain the original source"
    );

    await closeAutocomplete(browser);
  });

  it("preserves source selection when Escape is pressed", async () => {
    await openForm(win);

    const { browser, popup, row } = await openAutocomplete(
      win,
      "#email",
      mockEngineManager,
      { selectedSourceUrl: SOURCE_URL }
    );
    const { dialogManager, dialogWindow, tabSelector } = await openTabSelector(
      win,
      browser,
      row
    );

    await toggleTab(tabSelector, SOURCE_URL);
    await toggleTab(tabSelector, OTHER_SOURCE_URL);
    const restoredRow = await closeDialogAndWaitForAutocomplete(
      dialogManager,
      popup,
      () => EventUtils.synthesizeKey("KEY_Escape", {}, dialogWindow)
    );

    const sourceLabels = getSourceLabels(restoredRow);
    Assert.equal(
      sourceLabels.length,
      1,
      "The reopened row should show one source"
    );
    Assert.stringContains(
      sourceLabels[0],
      sourceTab.label,
      "The reopened row should retain the original source"
    );

    await closeAutocomplete(browser);
  });
});
