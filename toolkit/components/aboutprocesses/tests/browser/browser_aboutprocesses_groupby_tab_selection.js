/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(3);

add_task(async function testCheckboxTogglesSelection() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Checkbox toggle"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  let row = findTabRow(doc, "Checkbox toggle");
  let checkbox = row.querySelector(".select-checkbox");

  info("Clicking the checkbox selects the row");
  clickRow(checkbox);
  Assert.ok(checkbox.checked, "Checkbox is checked");
  Assert.ok(row.hasAttribute("selected"), "Row is selected");

  info("Clicking it again deselects the row");
  clickRow(checkbox);
  Assert.ok(!checkbox.checked, "Checkbox is unchecked");
  Assert.ok(!row.hasAttribute("selected"), "Row is no longer selected");

  info("Space on the focused checkbox toggles it too");
  checkbox.focus();
  EventUtils.synthesizeKey(" ", {}, doc.defaultView);
  Assert.ok(row.hasAttribute("selected"), "Space selects the row");
  EventUtils.synthesizeKey(" ", {}, doc.defaultView);
  Assert.ok(!row.hasAttribute("selected"), "Space deselects it again");

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
});

add_task(async function testSelectAllCheckboxSelectsAndClearsAll() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Select all 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Select all 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  let selectAll = doc.querySelector("#process-thead .select-checkbox");
  Assert.ok(selectAll, "Header select-all checkbox is rendered");

  info("Checking the header checkbox selects every row");
  clickRow(selectAll);
  for (let row of doc.querySelectorAll("tr.tab-row")) {
    Assert.ok(
      row.hasAttribute("selected"),
      `${row.tabData.title} is selected after select-all`
    );
  }
  Assert.ok(selectAll.checked, "Select-all checkbox reads as checked");
  Assert.ok(!selectAll.indeterminate, "Not indeterminate when all selected");

  info("Deselecting one row makes the header checkbox indeterminate");
  clickCheckbox(doc, "Select all 1");
  Assert.ok(!selectAll.checked, "No longer fully checked");
  Assert.ok(selectAll.indeterminate, "Indeterminate with a partial selection");

  info("Checking the header checkbox again selects every row again");
  clickRow(selectAll);
  for (let row of doc.querySelectorAll("tr.tab-row")) {
    Assert.ok(row.hasAttribute("selected"), "Every row selected again");
  }

  info("Unchecking the header checkbox clears the whole selection");
  clickRow(selectAll);
  for (let row of doc.querySelectorAll("tr.tab-row")) {
    Assert.ok(!row.hasAttribute("selected"), "Every row cleared");
  }
  Assert.ok(!selectAll.indeterminate, "Not indeterminate with none selected");

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testSelectionSurvivesPollAndPrunesClosedTabs() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Survives poll 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Survives poll 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  clickCheckbox(doc, "Survives poll 1");
  clickCheckbox(doc, "Survives poll 2");

  info("Forcing a poll tick while two rows are selected");
  await forceTabViewUpdate(tabAboutProcesses);
  Assert.ok(
    findTabRow(doc, "Survives poll 1").hasAttribute("selected"),
    "Selection survives a poll tick"
  );
  Assert.ok(
    findTabRow(doc, "Survives poll 2").hasAttribute("selected"),
    "Both selected rows survive the poll"
  );

  info("Closing a selected tab out from under the selection");
  let closedRow = findTabRow(doc, "Survives poll 1");
  let closedIndex = closedRow.rowIndex;
  await BrowserTestUtils.removeTab(tabOne);
  await forceTabViewUpdate(tabAboutProcesses);
  Assert.equal(
    closedRow.rowIndex,
    closedIndex,
    "The closed tab's row holds its place while the checkbox clicks above freeze reordering"
  );
  Assert.ok(
    closedRow.classList.contains("departed") && closedRow.inert,
    "...dimmed and inert, so it can't be acted on"
  );
  Assert.ok(
    findTabRow(doc, "Survives poll 2").hasAttribute("selected"),
    "The still-open tab's selection is untouched by the other tab closing"
  );
  Assert.equal(
    doc.querySelectorAll("tr.tab-row[selected]").length,
    1,
    "The closed tab no longer counts as selected"
  );

  info("Sorting ends the freeze");
  let updated = promiseTabViewUpdated(doc);
  doc.getElementById("column-name").click();
  await updated;
  Assert.ok(!closedRow.isConnected, "The closed tab's row is gone");

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testBulkActionBarShowsSelectionSummary() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Bulk summary 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Bulk summary 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  clickCheckbox(doc, "Bulk summary 1");
  clickCheckbox(doc, "Bulk summary 2");

  let summaryRow = doc.querySelector("tr.bulk-summary-row");
  Assert.ok(
    doc.body.classList.contains("bulk-action-visible"),
    "The visibility class is set while a selection exists"
  );

  Assert.equal(
    doc.defaultView.getComputedStyle(summaryRow).display,
    "grid",
    "The summary row uses the same per-column grid as every other row"
  );

  let countCell = summaryRow.querySelector(".bulk-count");
  await doc.l10n.translateFragment(summaryRow);
  Assert.ok(
    countCell.textContent.includes("2"),
    "The bulk-action bar's count reflects the two selected tabs"
  );

  let memoryText = summaryRow.querySelector(".memory").textContent;
  Assert.ok(
    /^[0-9.]+(B|KB|MB|GB)$/.test(memoryText),
    `Bulk-action memory "${memoryText}" is formatted as a number + unit, same as the totals row`
  );
  let cpuCell = summaryRow.querySelector(".cpu");
  Assert.ok(
    /^([0-9.]+%|idle|…|< 0\.01%)$/.test(cpuCell.textContent),
    `Bulk-action CPU "${cpuCell.textContent}" is formatted as a percent or idle, same as the totals row`
  );
  Assert.equal(
    doc.defaultView.getComputedStyle(cpuCell).backgroundImage,
    "none",
    "Bulk-action CPU cell has no CPU bar"
  );

  info("Deselecting both rows empties the selection and hides the bar");
  clickCheckbox(doc, "Bulk summary 1");
  clickCheckbox(doc, "Bulk summary 2");
  Assert.ok(
    !doc.body.classList.contains("bulk-action-visible"),
    "The visibility class is cleared once the selection is empty"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testBulkActionsReflectLoadedAndDiscardedSelection() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Bulk reload discarded"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Bulk reload loaded"
  );
  let { tabAboutProcesses, doc } = await openTabView();

  clickCheckbox(doc, "Bulk reload discarded");
  let unloadButton = doc.getElementById("bulk-unload-button");
  let reloadButton = doc.getElementById("bulk-reload-button");
  Assert.ok(
    !unloadButton.hidden,
    "Unload is shown when a loaded tab is selected"
  );

  findTabRow(doc, "Bulk reload discarded")
    .querySelector(".unload-icon")
    .click();
  await BrowserTestUtils.waitForMutationCondition(
    tabOne,
    { attributeFilter: ["discarded"] },
    () => tabOne.hasAttribute("discarded"),
    { msg: "The selected tab becomes discarded" }
  );
  await forceTabViewUpdate(tabAboutProcesses);

  clickCheckbox(doc, "Bulk reload loaded");
  Assert.ok(
    !unloadButton.hidden,
    "Mixed selection offers Unload for loaded tabs"
  );

  clickCheckbox(doc, "Bulk reload loaded");
  Assert.ok(unloadButton.hidden, "Discarded-only selection hides Unload");
  clickCheckbox(doc, "Bulk reload loaded");

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  let loadedTabReloaded = BrowserTestUtils.browserLoaded(tabTwo.linkedBrowser);
  reloadButton.click();
  await BrowserTestUtils.waitForMutationCondition(
    tabOne,
    { attributeFilter: ["discarded"] },
    () => !tabOne.hasAttribute("discarded"),
    { msg: "Bulk Reload restores the selected discarded tab" }
  );
  await loadedTabReloaded;
  Assert.ok(
    tabAboutProcesses.selected,
    "Bulk Reload leaves the monitor selected"
  );
  await Services.fog.testFlushAllChildren();

  assertOneEvent(
    Glean.resourceMonitor.tabActionTaken,
    { action_type: "reload", action_source: "bulk_bar", tab_count: "2" },
    "Bulk Reload records one action event"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
  Services.fog.testResetFOG();
});

add_task(async function testBulkActionBarCancelAndEscapeClearSelection() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Bulk cancel 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Bulk cancel 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  let rowOne = findTabRow(doc, "Bulk cancel 1");
  let rowTwo = findTabRow(doc, "Bulk cancel 2");
  clickRow(rowOne.querySelector(".select-checkbox"));
  clickRow(rowTwo.querySelector(".select-checkbox"));
  Assert.ok(
    doc.body.classList.contains("bulk-action-visible"),
    "Selection is active before cancelling"
  );

  doc.getElementById("bulk-cancel-button").click();
  Assert.ok(
    !doc.body.classList.contains("bulk-action-visible"),
    "The cancel icon clears the selection"
  );
  Assert.ok(!rowOne.hasAttribute("selected"), "Row one is no longer selected");
  Assert.ok(!rowTwo.hasAttribute("selected"), "Row two is no longer selected");

  info("Re-selecting, then cancelling via Escape instead");
  clickRow(rowOne.querySelector(".select-checkbox"));
  clickRow(rowTwo.querySelector(".select-checkbox"));
  Assert.ok(
    doc.body.classList.contains("bulk-action-visible"),
    "Selection is active again before the Escape test"
  );

  doc.getElementById("bulk-close-button").focus();
  EventUtils.synthesizeKey("KEY_Escape", {}, doc.defaultView);
  Assert.ok(
    !doc.body.classList.contains("bulk-action-visible"),
    "Escape also clears the selection, even with focus outside the rows"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testBulkActionBarUnloadButton() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Bulk unload 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Bulk unload 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  clickCheckbox(doc, "Bulk unload 1");
  clickCheckbox(doc, "Bulk unload 2");

  let buttonsRow = doc.querySelector("tr.bulk-buttons-row");
  pauseTabViewPolling(doc);
  buttonsRow.querySelector("#bulk-unload-button").click();

  await Promise.all(
    [tabOne, tabTwo].map(tab =>
      BrowserTestUtils.waitForMutationCondition(
        tab,
        { attributeFilter: ["discarded"] },
        () => tab.hasAttribute("discarded"),
        {
          msg: "Waiting for both selected tabs to be discarded by the bulk unload button",
        }
      )
    )
  );
  Assert.ok(
    !doc.body.classList.contains("bulk-action-visible"),
    "Unload clears the selection eagerly, hiding the bar immediately"
  );

  await BrowserTestUtils.waitForMutationCondition(
    doc.getElementById("process-tbody"),
    { subtree: true, attributeFilter: ["class"] },
    () =>
      findTabRow(doc, "Bulk unload 1")?.classList.contains("killed") &&
      findTabRow(doc, "Bulk unload 2")?.classList.contains("killed"),
    {
      msg: "Waiting for the bulk unload's own eager refresh to dim both rows, with no test-side update nudge",
    }
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testBulkActionBarCloseButton() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Bulk close 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Bulk close 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  clickCheckbox(doc, "Bulk close 1");
  clickCheckbox(doc, "Bulk close 2");
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();

  let closed = Promise.all([
    BrowserTestUtils.waitForTabClosing(tabOne),
    BrowserTestUtils.waitForTabClosing(tabTwo),
  ]);
  let buttonsRow = doc.querySelector("tr.bulk-buttons-row");
  pauseTabViewPolling(doc);
  buttonsRow.querySelector("#bulk-close-button").click();
  await closed;
  await Services.fog.testFlushAllChildren();
  assertOneEvent(
    Glean.resourceMonitor.tabActionTaken,
    { action_type: "close", action_source: "bulk_bar", tab_count: "2" },
    "Bulk Close records one action event for both selected tabs"
  );
  Assert.ok(
    !doc.body.classList.contains("bulk-action-visible"),
    "Close clears the selection eagerly, hiding the bar immediately"
  );

  await BrowserTestUtils.waitForMutationCondition(
    doc.getElementById("process-tbody"),
    { childList: true },
    () => !findTabRow(doc, "Bulk close 1") && !findTabRow(doc, "Bulk close 2"),
    {
      msg: "Waiting for the bulk close's own eager refresh to remove both rows, with no test-side update nudge",
    }
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
});

add_task(async function testBulkCloseHonorsCloseTabsWarning() {
  const { sinon } = ChromeUtils.importESModule(
    "resource://testing-common/Sinon.sys.mjs"
  );
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Bulk close warned 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Bulk close warned 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  clickCheckbox(doc, "Bulk close warned 1");
  clickCheckbox(doc, "Bulk close warned 2");

  let warn = sinon.stub(gBrowser, "warnAboutClosingTabs").returns(false);
  doc.querySelector("#bulk-close-button").click();
  warn.restore();

  Assert.ok(
    warn.calledOnceWith(2, gBrowser.constructor.closingTabsEnum.MULTI_SELECTED),
    "Bulk close asks the same multi-select close warning as the tab strip"
  );
  Assert.ok(
    tabOne.parentNode && tabTwo.parentNode,
    "Declining the warning keeps both tabs open"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

// A cross-window selection is grouped per tabbrowser, for every bulk action.
add_task(async function testBulkActionsSpanMultipleWindows() {
  for (let { action, promiseDone } of [
    {
      action: "close",
      promiseDone: tab => BrowserTestUtils.waitForTabClosing(tab),
    },
    {
      action: "unload",
      promiseDone: tab =>
        BrowserTestUtils.waitForMutationCondition(
          tab,
          { attributeFilter: ["discarded"] },
          () => tab.hasAttribute("discarded")
        ),
    },
  ]) {
    let tabOne = await setupTabWithOriginAndTitle(
      "https://example.org",
      `Cross-window ${action} 1`
    );
    let secondWindow = await BrowserTestUtils.openNewBrowserWindow();
    let tabTwo = await setupTabWithOriginAndTitle(
      "https://example.net",
      `Cross-window ${action} 2`,
      secondWindow
    );
    // The second window can steal OS focus on Windows, leaving this one hidden.
    await SimpleTest.promiseFocus(window);

    let { tabAboutProcesses, doc } = await openTabView();
    for (let n of [1, 2]) {
      clickCheckbox(doc, `Cross-window ${action} ${n}`);
    }
    let done = Promise.all([promiseDone(tabOne), promiseDone(tabTwo)]);
    doc.querySelector(`#bulk-${action}-button`).click();
    await done;

    BrowserTestUtils.removeTab(tabAboutProcesses);
    if (tabOne.isConnected) {
      BrowserTestUtils.removeTab(tabOne);
    }
    await BrowserTestUtils.closeWindow(secondWindow);
  }
});
