/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

requestLongerTimeout(3);

// Exercises the about:processes?groupby=tab dev/debug entry point: a flat
// per-tab list rendered by a separate View/Controller pair from the default
// process view (see ProcessesTabView/ProcessesTabController).

add_task(async function testGroupByTabRendersOneRowPerTab() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Groupby tab test 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Groupby tab test 2"
  );

  info("Opening about:processes?groupby=tab");
  let { tabAboutProcesses, doc } = await openTabView();

  for (let title of ["Groupby tab test 1", "Groupby tab test 2"]) {
    let row = findTabRow(doc, title);
    Assert.ok(!!row, `Found a row for tab titled "${title}"`);

    // Locale-aware formatting (document.l10n.setAttributes, not .toFixed())
    // still renders as a plain number + unit in en-US, but resolves async --
    // unlike the .toFixed() it replaced, so cell text isn't guaranteed to be
    // there the instant the row itself is.
    let memoryCell = row.querySelector(".memory");
    await TestUtils.waitForCondition(
      () => memoryCell.textContent,
      "Waiting for Fluent to resolve the memory cell's text"
    );
    Assert.ok(
      /^[0-9.]+(B|KB|MB|GB)$/.test(memoryCell.textContent),
      `Memory cell "${memoryCell.textContent}" is formatted as a number + unit`
    );

    let cpuCell = row.querySelector(".cpu");
    await TestUtils.waitForCondition(
      () => cpuCell.textContent,
      "Waiting for Fluent to resolve the CPU cell's text"
    );
    // A freshly opened test tab may legitimately show as idle, depending on
    // incidental CPU noise, or as measuring if its process started after the
    // first sample.
    Assert.ok(
      /^([0-9.]+%|idle|…|< 0\.01%)$/.test(cpuCell.textContent),
      `CPU cell "${cpuCell.textContent}" is formatted as a percent or idle`
    );
  }

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testTabCpuPrecisionAndIdleThreshold() {
  let tab = await setupTabWithOriginAndTitle(
    "https://example.org",
    "CPU formatting"
  );
  let { tabAboutProcesses, doc } = await openTabView();
  // So no poll repaints the row between the fake render and the read.
  pauseTabViewPolling(doc);

  async function formatCell(selector, overrides) {
    return SpecialPowers.spawn(
      tabAboutProcesses.linkedBrowser,
      [selector, overrides],
      async (cellSelector, data) => {
        let row = content.document.querySelector("tr.tab-row");
        let cell = row.querySelector(cellSelector);
        content.TabView._updateRow(row, { ...row.tabData, ...data });
        await content.document.l10n.translateElements([cell]);
        return {
          l10nId: cell.getAttribute("data-l10n-id"),
          text: cell.textContent,
        };
      }
    );
  }
  let formatCpu = slopeCpuOfTotal => formatCell(".cpu", { slopeCpuOfTotal });
  let formatMemory = async totalRamSize =>
    (await formatCell(".memory", { totalRamSize })).text;

  let idle = await formatCpu(0);
  Assert.equal(
    idle.l10nId,
    "about-processes-tab-cpu-fully-idle",
    "Exactly zero CPU usage remains idle"
  );

  let roundsToZero = await formatCpu(0.000025);
  Assert.equal(
    roundsToZero.text,
    "< 0.01%",
    "A positive value below the 0.01% display precision has its own label"
  );

  let visibleValue = await formatCpu(0.00029);
  Assert.equal(
    visibleValue.text,
    "0.03%",
    "A value above the display threshold is rounded to two decimal places"
  );

  let wholeValue = await formatCpu(0.02);
  Assert.equal(
    wholeValue.text,
    "2.00%",
    "Whole percentages keep both decimal places"
  );

  Assert.equal(
    await formatMemory(518 * 1024 * 1024),
    "518MB",
    "Megabytes stay whole"
  );
  Assert.equal(
    await formatMemory(1.42 * 1024 * 1024 * 1024),
    "1.4GB",
    "Gigabytes show one decimal place"
  );
  Assert.equal(
    await formatMemory(2 * 1024 * 1024 * 1024),
    "2.0GB",
    "Whole gigabytes keep their decimal place"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function testAboutBlankTabIsIncluded() {
  // Regression test: the tab list used to silently drop about:blank tabs,
  // even though a genuinely open about:blank tab (e.g. a freshly opened
  // empty tab) is a real row a user would expect to see.
  let tab = BrowserTestUtils.addTab(gBrowser, "about:blank");
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, false, "about:blank");

  let { tabAboutProcesses, doc } = await openTabView();

  let rows = [...doc.querySelectorAll("tr.tab-row")];
  Assert.ok(
    rows.some(row => row.tabData.tab == tab),
    "The about:blank tab has its own row"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function testInitialSortIsVisible() {
  let { tabAboutProcesses, doc } = await openTabView();

  // Regression test: rows render memory-descending by default, but with no
  // caret/aria-sort anywhere, so the first click on Memory looked like a
  // no-op.
  let memoryHeader = doc.getElementById("column-memory-resident");
  Assert.equal(
    memoryHeader.getAttribute("aria-sort"),
    "descending",
    "The initial memory-descending sort is reflected in aria-sort"
  );
  Assert.ok(
    memoryHeader.classList.contains("arrow-down"),
    "The initial sort shows a caret on the Memory column"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
});

add_task(async function testCpuIsMeasuringBeforeSecondSample() {
  let { tabAboutProcesses, doc } = await openTabView();
  let win = doc.defaultView;

  // Every process's state right after its first sample.
  let counters = win.State.getCounters().map(process => ({
    ...process,
    slopeCpu: null,
  }));
  let tabCounters = win.TabControl._tabAttribution
    .getTabCounters(counters)
    .filter(tabData => tabData.hasUsageData);
  Assert.ok(tabCounters.length, "Some tab has usage data");
  Assert.ok(
    tabCounters.every(tabData => tabData.slopeCpuOfTotal === null),
    "A tab's CPU is unknown until its processes have two samples"
  );
  Assert.strictEqual(
    win.sumTabCounters(tabCounters).slopeCpuOfTotal,
    null,
    "...and so is any total including it"
  );
  let cell = doc.createElement("td");
  win.fillTabCpuCell(cell, null);
  Assert.equal(cell.textContent, "…", "Unknown CPU shows as measuring");

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  let lastTabCounters = win.TabControl._lastTabCounters;
  try {
    win.TabControl._lastTabCounters = tabCounters;
    win.TabControl._recordTabAction(tabCounters, "unload", "bulk_bar");
  } finally {
    win.TabControl._lastTabCounters = lastTabCounters;
  }
  await Services.fog.testFlushAllChildren();
  let event = assertOneEvent(
    Glean.resourceMonitor.tabActionTaken,
    { action_type: "unload", action_source: "bulk_bar" },
    "An action with unknown CPU still records the user's choice"
  );
  Assert.ok(
    !("tab_cpu_hundredths" in event.extra),
    "Unknown tab CPU is omitted"
  );
  Assert.ok(
    !("total_cpu_hundredths" in event.extra),
    "Unknown total CPU is omitted"
  );
  Services.fog.testResetFOG();

  BrowserTestUtils.removeTab(tabAboutProcesses);
});

add_task(async function testMouseoverFreezesExistingRowOrderOnly() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Freeze test 1"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  let tbody = doc.getElementById("process-tbody");
  let rowOne = findTabRow(doc, "Freeze test 1");
  let rowOneIndex = [...tbody.children].indexOf(rowOne);

  startMouseFreeze(doc);

  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Freeze test 2"
  );
  await forceTabViewUpdate(tabAboutProcesses);
  let rowTwo = findTabRow(doc, "Freeze test 2");
  Assert.ok(
    !!rowTwo,
    "A newly opened tab's row appears even while a recent mouse event freezes existing row order"
  );
  Assert.equal(
    [...tbody.children].indexOf(rowOne),
    rowOneIndex,
    "An existing row's position doesn't move while frozen"
  );
  Assert.equal(
    tbody.lastElementChild,
    rowTwo,
    "The new row is appended at the end, not sorted into place, until the next real reorder"
  );

  info("Clicking a column header forces the reorder despite the freeze");
  let updated = promiseTabViewUpdated(doc);
  doc.getElementById("column-name").click();
  await updated;
  Assert.ok(
    !!findTabRow(doc, "Freeze test 2"),
    "The new row is still shown after an explicit sort"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testFaviconAndColumnSort() {
  // Site order (example.net < example.org) is the reverse of title order, so
  // solo rows must sort by their displayed title, not their site.
  let tabZebra = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Zebra tab"
  );
  let tabApple = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Apple tab"
  );
  // Set directly on the <tab> element rather than relying on a real favicon
  // fetch for these test domains, which isn't reliable in this harness.
  tabZebra.setAttribute("image", "chrome://global/skin/icons/link.svg");

  let { tabAboutProcesses, doc } = await openTabView();

  let zebraRow = findTabRow(doc, "Zebra tab");
  Assert.ok(
    zebraRow.querySelector(".name").style.backgroundImage.includes("link.svg"),
    "The tab's favicon is rendered as the name cell's background image"
  );

  info("Clicking the Name column header to sort ascending (alphabetical)");
  let updated = promiseTabViewUpdated(doc);
  doc.getElementById("column-name").click();
  await updated;

  let nameHeader = doc.getElementById("column-name");
  Assert.equal(
    nameHeader.getAttribute("aria-sort"),
    "ascending",
    "Name column reports ascending via aria-sort"
  );
  let titles = tabRowTitles(doc);
  Assert.less(
    titles.indexOf("Apple tab"),
    titles.indexOf("Zebra tab"),
    'Ascending name sort puts "Apple tab" before "Zebra tab"'
  );

  info("Clicking Name again to reverse to descending");
  updated = promiseTabViewUpdated(doc);
  doc.getElementById("column-name").click();
  await updated;

  titles = tabRowTitles(doc);
  Assert.greater(
    titles.indexOf("Apple tab"),
    titles.indexOf("Zebra tab"),
    "Clicking the same column again reverses the sort order"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabZebra);
  BrowserTestUtils.removeTab(tabApple);
});

add_task(async function testNameLinkClickNavigatesToTab() {
  // Only the name link navigates; the rest of the row does nothing.
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Click me"
  );

  let { tabAboutProcesses, doc } = await openTabView();

  let row = findTabRow(doc, "Click me");
  Assert.ok(!!row, "Found the row for the tab we're about to switch to");
  Assert.ok(
    !tabOne.selected,
    "about:processes tab is the one selected right now"
  );

  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  info("Clicking the name link");
  clickRow(row.querySelector(".name-link"));

  Assert.ok(tabOne.selected, "Clicking the name link switched to the tab");
  await Services.fog.testFlushAllChildren();
  assertOneEvent(
    Glean.resourceMonitor.tabActionTaken,
    { action_type: "navigate", action_source: "tab_row" },
    "Navigating records one action event"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
});

add_task(async function testUnloadButtonAndKeyboardActivation() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Unload via button"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Unload via keyboard"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  // Only the actions' own refreshes and poll() can update the rows.
  let poll = pauseTabViewPolling(doc);

  let rowOne = findTabRow(doc, "Unload via button");
  Assert.ok(!!rowOne, "Found the row for the tab we're about to unload");
  Assert.equal(
    rowOne.tabIndex,
    -1,
    "The row itself isn't a Tab stop -- only its unload button is"
  );

  let unloadButtonOne = rowOne.querySelector(".unload-icon");
  Assert.equal(
    unloadButtonOne.tabIndex,
    0,
    "The unload button is a real Tab stop, matching the default view's kill button"
  );

  info(
    "Confirming Enter on the focused unload button also unloads its tab, " +
      "matching the default view's own Enter/Space-activates-a-focused-" +
      "button behavior"
  );
  let rowTwo = findTabRow(doc, "Unload via keyboard");
  let unloadButtonTwo = rowTwo.querySelector(".unload-icon");
  unloadButtonTwo.focus();
  EventUtils.synthesizeKey("KEY_Enter", {}, doc.defaultView);
  await TestUtils.waitForCondition(
    () => tabTwo.hasAttribute("discarded"),
    "Waiting for the Enter-activated tab to be discarded"
  );
  Assert.ok(rowTwo.isConnected, "Enter-unloaded row also stays in the list");
  await poll();
  await promiseRowActionDone(rowTwo);

  info(
    "Regression test: a discarded row's button used to be hidden, leaving " +
      "keyboard users with no way to reach it at all -- unlike a mouse " +
      "user, who could still double-click the row to get there. Confirming " +
      "the still-focused button now relabels to reload and Enter restores it."
  );
  Assert.ok(
    !unloadButtonTwo.hidden,
    "The button stays visible and focusable once discarded"
  );
  Assert.ok(
    !unloadButtonTwo.classList.contains("unload-icon") &&
      unloadButtonTwo.classList.contains("reload-icon"),
    "The button's icon swaps once the tab is discarded"
  );
  Assert.equal(
    unloadButtonTwo.getAttribute("data-l10n-id"),
    "about-processes-reload-tab",
    "The same button relabels itself to reload once the tab is discarded"
  );
  Assert.ok(!tabTwo.selected, "The discarded tab isn't selected yet");
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  EventUtils.synthesizeKey("KEY_Enter", {}, doc.defaultView);
  await BrowserTestUtils.waitForMutationCondition(
    tabTwo,
    { attributeFilter: ["discarded"] },
    () => !tabTwo.hasAttribute("discarded"),
    { msg: "Enter on the relabeled button reloads the discarded tab" }
  );
  Assert.ok(
    tabAboutProcesses.selected && !tabTwo.selected,
    "Reloading a background tab doesn't navigate away from the monitor"
  );
  await Services.fog.testFlushAllChildren();
  assertOneEvent(
    Glean.resourceMonitor.tabActionTaken,
    { action_type: "reload", action_source: "tab_row" },
    "Inline Reload records one action event"
  );
  Services.fog.testResetFOG();

  info("Double-clicking unload acts once");
  unloadButtonOne.click();
  unloadButtonOne.click();

  await TestUtils.waitForCondition(
    () => tabOne.hasAttribute("discarded"),
    "Waiting for the clicked tab to be discarded"
  );
  Assert.ok(rowOne.isConnected, "Unloading a tab keeps its row in the list");
  await Services.fog.testFlushAllChildren();
  let unloadEvent = assertOneEvent(
    Glean.resourceMonitor.tabActionTaken,
    { action_type: "unload", action_source: "tab_row", tab_count: "1" },
    "Inline Unload records one action event"
  );
  Assert.greaterOrEqual(
    Number(unloadEvent?.extra.total_tab_count),
    2,
    "The view's total tab count includes both tabs we opened"
  );

  await BrowserTestUtils.waitForMutationCondition(
    rowOne,
    { attributeFilter: ["class"] },
    () => rowOne.classList.contains("killed"),
    { msg: "Unloading refreshes the row without waiting for a poll" }
  );

  info(
    "Confirming a poll after the discard settles the row into the static killed look, not removal"
  );
  await poll();
  Assert.ok(rowOne.isConnected, "Row is still in the list after a poll tick");
  Assert.ok(
    rowOne.classList.contains("killed"),
    "Discarded tab's row shows the static dimmed state"
  );
  Assert.ok(
    !rowOne.classList.contains("killing"),
    "Transient fade-out class is cleared once the discard is confirmed"
  );

  info("Clicking the button on a discarded row reloads that tab");
  await promiseRowActionDone(rowOne);
  unloadButtonOne.click();
  Assert.ok(
    unloadButtonOne.classList.contains("unload-icon"),
    "Reloading shows the unload button again right away"
  );
  await BrowserTestUtils.waitForMutationCondition(
    tabOne,
    { attributeFilter: ["discarded"] },
    () => !tabOne.hasAttribute("discarded"),
    { msg: "Clicking the relabeled button reloads the discarded tab" }
  );
  Assert.ok(
    tabAboutProcesses.selected && !tabOne.selected,
    "Reloading a discarded background tab leaves the monitor selected"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testUnloadButtonDoesNotLeaveIneligibleTabBusy() {
  let tab = BrowserTestUtils.addTab(gBrowser, "about:robots", {
    forceNotRemote: true,
  });
  await BrowserTestUtils.browserLoaded(tab.linkedBrowser, true, "about:robots");

  let { tabAboutProcesses, doc } = await openTabView();
  let row = findTabRow(doc, tab.label);
  Assert.ok(!!row, "Found the non-remote tab in the tab view");

  row.querySelector(".unload-icon").click();
  await TestUtils.waitForCondition(
    () => !row.classList.contains("killing") && !row.hasAttribute("aria-busy"),
    "A tab that cannot be unloaded is not left in a fake busy state"
  );
  Assert.ok(
    !tab.hasAttribute("discarded"),
    "The ineligible tab remains loaded"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tab);
});

add_task(async function testPrivateWindowTabsExcludedFromNonPrivateView() {
  // Regression test: a non-private groupby=tab view used to show private
  // windows' tab titles/URLs with no exclusion or marker -- a privacy
  // hazard, not just a missing label.
  let privateWindow = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  let privateTab = BrowserTestUtils.addTab(
    privateWindow.gBrowser,
    "https://example.org",
    { skipAnimation: true }
  );
  await BrowserTestUtils.browserLoaded(privateTab.linkedBrowser);
  await SpecialPowers.spawn(
    privateTab.linkedBrowser,
    ["Private tab should not leak"],
    async title => {
      content.document.title = title;
    }
  );

  // Opening the private window took OS focus with it, which on Windows can
  // leave this (non-private) window's soon-to-be-opened tab reporting
  // document.hidden -- refocus before relying on it to actually poll.
  await SimpleTest.promiseFocus(window);

  let { tabAboutProcesses, doc } = await openTabView();

  Assert.ok(
    !findTabRow(doc, "Private tab should not leak"),
    "A non-private groupby=tab view must not show a private window's tabs"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  await BrowserTestUtils.closeWindow(privateWindow);
});

add_task(async function testDefaultViewUnaffectedByGroupByParam() {
  info("Opening plain about:processes to confirm the default view is intact");
  let tabAboutProcesses = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:processes",
    waitForLoad: true,
  });

  let doc = tabAboutProcesses.linkedBrowser.contentDocument;
  await promiseAboutProcessesUpdated({
    doc,
    force: true,
    tabAboutProcesses,
  });

  Assert.ok(
    !!doc.querySelector("tr.process"),
    "The default view should still render process rows"
  );
  for (let id of ["column-memory-resident", "column-cpu-total"]) {
    let header = doc.getElementById(id);
    Assert.equal(
      doc.defaultView.getComputedStyle(header).textAlign,
      "start",
      `${id} retains the default view's start-aligned heading`
    );
    Assert.equal(
      doc.defaultView.getComputedStyle(header).fontWeight,
      doc.defaultView.getComputedStyle(header.parentElement).fontWeight,
      `${id} retains inherited header weight in the default view`
    );
  }
  Assert.ok(
    !doc.querySelector("tr.tab-row"),
    "The default view should never render tab-view rows"
  );
  Assert.equal(
    doc.defaultView.getComputedStyle(doc.getElementById("process-tfoot"))
      .display,
    "none",
    "The default view should not show the tab view's totals footer"
  );
  Assert.equal(
    doc.defaultView.getComputedStyle(
      doc.getElementById("select-all-checkbox").parentElement
    ).display,
    "none",
    "The default view should not show the select-all column"
  );
  Assert.equal(
    doc.defaultView.getComputedStyle(
      doc.getElementById("column-name").parentElement
    ).borderInlineStartWidth,
    "0px",
    "The hidden select-all column doesn't give the Name header a divider"
  );
  Assert.equal(
    doc.defaultView.getComputedStyle(doc.querySelector("tr.process > td"))
      .textOverflow,
    "ellipsis",
    "Long process names still truncate with an ellipsis"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
});

add_task(async function testTotalsBarShowsTabCountAndSums() {
  // Anchor tab: about:blank and about:processes' own tab are excluded from the list.
  let anchorTab = await setupTabWithOriginAndTitle(
    "https://example.com",
    "Totals bar anchor"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  let totalsRow = doc.querySelector("tr.totals-row");
  Assert.ok(totalsRow, "Totals row is rendered in the tab view");
  let countCell = totalsRow.querySelector(".totals-count");

  await doc.l10n.translateRoots();
  // Baseline includes whatever's already open; only the +2 delta below matters.
  let baselineCount = parseInt(countCell.textContent, 10);

  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Totals bar test 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Totals bar test 2"
  );
  await forceTabViewUpdate(tabAboutProcesses);
  await doc.l10n.translateRoots();
  Assert.equal(
    parseInt(countCell.textContent, 10),
    baselineCount + 2,
    "The totals bar's tab count includes the two new tabs"
  );

  let memoryText = totalsRow.querySelector(".memory").textContent;
  Assert.ok(
    /^[0-9.]+(B|KB|MB|GB)$/.test(memoryText),
    `Totals memory "${memoryText}" is formatted as a number + unit, same as row cells`
  );
  let cpuCell = totalsRow.querySelector(".cpu");
  Assert.ok(
    /^([0-9.]+%|idle|…|< 0\.01%)$/.test(cpuCell.textContent),
    `Totals CPU "${cpuCell.textContent}" is formatted as a percent or idle, same as row cells`
  );
  Assert.equal(
    doc.defaultView.getComputedStyle(cpuCell).backgroundImage,
    "none",
    "Totals CPU cell has no CPU bar"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
  BrowserTestUtils.removeTab(anchorTab);
});

add_task(async function testPanelOpenedTelemetry() {
  await Services.fog.testFlushAllChildren();
  Services.fog.testResetFOG();
  let tab = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Panel opened telemetry"
  );

  // openTabView() already drives two polls, so CPU is known.
  let { tabAboutProcesses } = await openTabView();
  await Services.fog.testFlushAllChildren();

  // openTabView() opens a tab directly, bypassing SidebarController.
  let event = assertOneEvent(
    Glean.resourceMonitor.panelOpened,
    { open_type: "direct_nav" },
    "panel_opened fired exactly once"
  );
  Assert.greaterOrEqual(
    Number(event?.extra.total_tab_count),
    1,
    "Counted at least the one tab we opened."
  );

  info("A further poll tick must not re-fire the event.");
  await forceTabViewUpdate(tabAboutProcesses);
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.resourceMonitor.panelOpened.testGetValue()?.length,
    1,
    "Still only one panel_opened event after a third poll."
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tab);
  Services.fog.testResetFOG();
});

add_task(async function testPanelOpenedIgnoresUnknownOpenType() {
  let tabAboutProcesses = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:processes?groupby=tab&openType=foo",
    waitForLoad: true,
  });
  await forceTabViewUpdate(tabAboutProcesses);
  await Services.fog.testFlushAllChildren();

  assertOneEvent(
    Glean.resourceMonitor.panelOpened,
    { open_type: "direct_nav" },
    "An unrecognized openType is recorded as direct navigation"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  Services.fog.testResetFOG();
});

add_task(async function testPanelOpenedSkipsHiddenPolls() {
  // Not openTabView(): it forces the second poll immediately, with no room to insert a hidden gap.
  let tab = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Panel opened skips hidden polls"
  );

  let tabAboutProcesses = await BrowserTestUtils.openNewForegroundTab({
    gBrowser,
    opening: "about:processes?groupby=tab",
    waitForLoad: true,
  });
  // The first (real) poll fires during window.onload, before we get here.
  let doc = tabAboutProcesses.linkedBrowser.contentDocument;
  await BrowserTestUtils.waitForMutationCondition(
    doc.getElementById("process-tbody"),
    { childList: true },
    () => doc.querySelector("tr.tab-row"),
    { msg: "Waiting for the first poll to render a row" }
  );

  let poll = pauseTabViewPolling(doc);
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.resourceMonitor.panelOpened.testGetValue()?.length ?? 0,
    0,
    "Only the first poll has run"
  );

  let hidden = BrowserTestUtils.waitForEvent(doc, "visibilitychange");
  gBrowser.selectedTab = tab;
  await hidden;

  await poll();
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.resourceMonitor.panelOpened.testGetValue()?.length ?? 0,
    0,
    "panel_opened must not fire from a poll that lands while hidden"
  );

  let visible = BrowserTestUtils.waitForEvent(doc, "visibilitychange");
  gBrowser.selectedTab = tabAboutProcesses;
  await visible;

  await poll();
  await Services.fog.testFlushAllChildren();
  Assert.equal(
    Glean.resourceMonitor.panelOpened.testGetValue()?.length,
    1,
    "panel_opened fires exactly once, on the first real poll after visibility returns"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tab);
  Services.fog.testResetFOG();
});
