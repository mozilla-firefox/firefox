/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function testSearchFiltersTabsAndRestoresGroups() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Search alpha"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Search beta"
  );
  let { tabAboutProcesses, doc } = await openTabView();
  try {
    await doc.defaultView.customElements.whenDefined("moz-input-search");
    let search = doc.getElementById("tab-search-input");
    await search.updateComplete;
    let header = findSiteHeaderRow(doc, "example.org");
    Assert.ok(header, "The two fixture tabs form a site group");
    Assert.ok(
      childRowsOf(header).every(row => BrowserTestUtils.isHidden(row)),
      "The group starts collapsed"
    );

    await enterTabSearch(doc, "  ALPHA  ");
    Assert.deepEqual(
      tabRowTitles(doc),
      ["Search alpha"],
      "Typing filters titles case-insensitively and trims whitespace"
    );
    Assert.ok(
      BrowserTestUtils.isVisible(findTabRow(doc, "Search alpha")),
      "A matching child of a collapsed group becomes visible"
    );

    await enterTabSearch(doc, "EXAMPLE.ORG");
    header = findSiteHeaderRow(doc, "example.org");
    Assert.equal(childRowsOf(header).length, 2, "A site match keeps both tabs");
    Assert.ok(
      childRowsOf(header).every(row => BrowserTestUtils.isVisible(row)),
      "Search expands matching groups"
    );
    let globalToggle = doc.getElementById("expand-all-button");
    Assert.ok(
      globalToggle.disabled,
      "The expand-all toggle is disabled during search"
    );
    Assert.ok(header.caret.disabled, "Carets are disabled during search");
    Assert.equal(
      header.caret.getAttribute("aria-expanded"),
      "true",
      "...and report the group as expanded"
    );

    info("A selected tab stays listed when a search doesn't match it");
    clickCheckbox(doc, "Search beta");
    await enterTabSearch(doc, "ALPHA");
    Assert.deepEqual(
      tabRowTitles(doc).sort(),
      ["Search alpha", "Search beta"],
      "The match is listed, along with the selected non-match"
    );
    Assert.ok(
      findTabRow(doc, "Search beta").hasAttribute("selected"),
      "The selected non-match keeps its selection"
    );
    await enterTabSearch(doc, "no-matching-tab-12345");
    Assert.deepEqual(
      tabRowTitles(doc),
      ["Search beta"],
      "With no matches, only the selected tab is listed"
    );
    let noResults = doc.getElementById("tab-search-no-results");
    await doc.l10n.translateElements([noResults]);
    Assert.ok(
      noResults.textContent.includes("no-matching-tab-12345"),
      "The no-results message shows despite the listed selection"
    );
    doc.getElementById("bulk-cancel-button").click();
    await enterTabSearch(doc, "EXAMPLE.ORG");

    await enterTabSearch(doc, "no-matching-tab-12345");
    Assert.deepEqual(
      tabRowTitles(doc),
      [],
      "An unmatched query hides every tab"
    );
    Assert.ok(
      BrowserTestUtils.isHidden(findSiteHeaderRow(doc, "example.org")),
      "...and the site header"
    );
    await doc.l10n.translateElements([noResults]);
    Assert.ok(
      noResults.textContent.includes("no-matching-tab-12345"),
      "The no-results message is shown"
    );
    await forceTabViewUpdate(tabAboutProcesses);
    Assert.ok(
      noResults.textContent.includes("no-matching-tab-12345"),
      "The no-results message survives polling"
    );

    search.clear();
    header = findSiteHeaderRow(doc, "example.org");
    Assert.equal(childRowsOf(header).length, 2, "Clearing restores both tabs");
    Assert.ok(
      childRowsOf(header).every(row => BrowserTestUtils.isHidden(row)),
      "Clearing restores the previous collapsed state"
    );
    Assert.ok(
      !globalToggle.disabled,
      "Clearing the search reactivates the toggle"
    );
    Assert.ok(!header.caret.disabled, "Clearing the search re-enables carets");
    Assert.equal(
      noResults.textContent,
      "",
      "Clearing removes the no-results message"
    );
  } finally {
    BrowserTestUtils.removeTab(tabAboutProcesses);
    BrowserTestUtils.removeTab(tabOne);
    BrowserTestUtils.removeTab(tabTwo);
  }
});

add_task(async function testSearchVisibilityWaitsOutTheMouseFreeze() {
  let tabs = [
    await setupTabWithOriginAndTitle("https://example.org/first", "Needle A"),
    await setupTabWithOriginAndTitle("https://example.org/second", "Other B"),
  ];
  let { tabAboutProcesses, doc } = await openTabView();
  try {
    await enterTabSearch(doc, "needle");
    let header = findSiteHeaderRow(doc, "example.org");
    Assert.ok(
      BrowserTestUtils.isVisible(header),
      "A partly matching site keeps its header"
    );
    Assert.deepEqual(tabRowTitles(doc), ["Needle A"], "Only the match shows");
    let rowB = findTabRow(doc, "Other B");

    await SpecialPowers.spawn(tabs[1].linkedBrowser, [], () => {
      content.document.title = "Needle B";
    });
    await BrowserTestUtils.waitForMutationCondition(
      tabs[1],
      { attributeFilter: ["label"] },
      () => tabs[1].label == "Needle B",
      { msg: "The new title has reached browser chrome" }
    );
    startMouseFreeze(doc);
    await forceTabViewUpdate(tabAboutProcesses);
    Assert.ok(
      rowB.hidden,
      "During the mouse freeze, a newly matching tab stays hidden"
    );

    info("Re-sorting ends the freeze");
    let updated = promiseTabViewUpdated(doc);
    doc.getElementById("column-name").click();
    await updated;
    Assert.ok(!rowB.hidden, "The reorder shows the newly matching tab");
  } finally {
    BrowserTestUtils.removeTab(tabAboutProcesses);
    for (let tab of tabs) {
      BrowserTestUtils.removeTab(tab);
    }
  }
});

add_task(async function testSelectionControlsSkipHiddenTabs() {
  let tabs = [
    await setupTabWithOriginAndTitle("https://example.org/first", "Needle A"),
    await setupTabWithOriginAndTitle("https://example.org/second", "Other B"),
  ];
  let { tabAboutProcesses, doc } = await openTabView();
  try {
    await enterTabSearch(doc, "needle");
    let header = findSiteHeaderRow(doc, "example.org");
    let selectAll = doc.getElementById("select-all-checkbox");

    clickRow(header.checkbox);
    Assert.ok(
      header.checkbox.checked && !header.checkbox.indeterminate,
      "Every shown tab of the site is selected"
    );
    Assert.ok(selectAll.checked, "...which is every shown tab");
    Assert.ok(
      !findTabRow(doc, "Other B").checkbox.checked,
      "The site checkbox skips the hidden tab"
    );

    clickRow(header.checkbox);
    clickRow(selectAll);
    Assert.ok(
      findTabRow(doc, "Needle A").checkbox.checked,
      "Select-all selects the shown tab"
    );
    Assert.ok(
      !findTabRow(doc, "Other B").checkbox.checked,
      "...but not the hidden one"
    );
  } finally {
    BrowserTestUtils.removeTab(tabAboutProcesses);
    for (let tab of tabs) {
      BrowserTestUtils.removeTab(tab);
    }
  }
});

add_task(async function testTotalsFollowSearchAndSelection() {
  let fixtures = [
    {
      title: "Totals matching",
      origin: "https://example.org",
      ram: 2883584,
      cpu: 0.015625,
    },
    {
      title: "Totals selected",
      origin: "https://example.org",
      ram: 5898240,
      cpu: 0.03125,
    },
    {
      title: "Totals hidden",
      origin: "https://example.org",
      ram: 12320768,
      cpu: 0.0625,
    },
    {
      title: "Totals outside",
      origin: "https://example.net",
      ram: 18481152,
      cpu: 0.125,
    },
  ];
  let tabs = [];
  for (let { origin, title } of fixtures) {
    tabs.push(await setupTabWithOriginAndTitle(origin, title));
  }
  let { tabAboutProcesses, doc } = await openTabView();
  try {
    let control = doc.defaultView.TabControl;
    let measured = fixtures.map(({ title, ram, cpu }) => ({
      ...control._lastTabCounters.find(tabData => tabData.title == title),
      totalRamSize: ram,
      slopeCpuOfTotal: cpu,
      hasUsageData: true,
    }));
    Assert.ok(
      measured.every(tabData => tabData.tab),
      "Found every fixture tab"
    );
    // Fixed usage numbers, so the sums below are exact.
    control._tabAttribution.getTabCounters = () => measured;
    await forceTabViewUpdate(tabAboutProcesses);

    function assertUsage(row, ramMb, cpu, description) {
      let memory = doc.l10n.getAttributes(row.querySelector(".memory"));
      let processor = doc.l10n.getAttributes(row.querySelector(".cpu"));
      Assert.equal(memory.args.total, ramMb, `${description} memory`);
      Assert.equal(memory.args.totalUnit, "MB", `${description} memory unit`);
      Assert.equal(processor.args.percent, cpu, `${description} CPU`);
    }

    clickRow(
      findSiteHeaderRow(doc, "example.org").querySelector(".caret-icon")
    );
    clickCheckbox(doc, "Totals selected");
    clickCheckbox(doc, "Totals outside");
    await enterTabSearch(doc, "matching");
    Assert.deepEqual(
      tabRowTitles(doc).sort(),
      ["Totals matching", "Totals outside", "Totals selected"],
      "Search lists the match and the selected non-matches"
    );
    let totalsRow = doc.querySelector("tr.totals-row");
    Assert.equal(
      doc.l10n.getAttributes(totalsRow.firstElementChild).args.count,
      4,
      "The footer counts the tab hidden by search"
    );
    assertUsage(totalsRow, 37.75, 0.234375, "The footer");
    assertUsage(
      doc.querySelector("tr.bulk-summary-row"),
      23.25,
      0.15625,
      "The selection subtotal"
    );
    assertUsage(
      findSiteHeaderRow(doc, "example.org"),
      8.375,
      0.046875,
      "The filtered site group"
    );
  } finally {
    BrowserTestUtils.removeTab(tabAboutProcesses);
    for (let tab of tabs) {
      BrowserTestUtils.removeTab(tab);
    }
  }
});
