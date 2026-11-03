/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function testTotalsWithMissingData() {
  let titles = ["Totals selected", "Totals other"];
  let tabs = [];
  for (let title of titles) {
    tabs.push(await setupTabWithOriginAndTitle("https://example.org", title));
  }
  let { tabAboutProcesses, doc } = await openTabView();
  try {
    let control = doc.defaultView.TabControl;
    let current = control._lastTabCounters.map(tabData => ({
      ...tabData,
      totalRamSize: 0,
      slopeCpuOfTotal: 0,
      hasUsageData: false,
    }));
    control._tabAttribution.getTabCounters = () => current;
    await forceTabViewUpdate(tabAboutProcesses);
    clickRow(
      findSiteHeaderRow(doc, "example.org").querySelector(".caret-icon")
    );
    clickCheckbox(doc, "Totals selected");

    let totalRows = () => [
      findSiteHeaderRow(doc, "example.org"),
      doc.querySelector("tr.bulk-summary-row"),
      doc.querySelector("tr.totals-row"),
    ];
    for (let row of totalRows()) {
      for (let cell of row.querySelectorAll(".memory, .cpu")) {
        Assert.equal(cell.textContent, "—", "No measured tab reads as no data");
        Assert.ok(
          !cell.hasAttribute("data-l10n-id"),
          "No stale number is left behind"
        );
      }
    }

    current.find(tabData => tabData.title == "Totals selected").hasUsageData =
      true;
    await forceTabViewUpdate(tabAboutProcesses);
    for (let row of totalRows()) {
      let cells = [...row.querySelectorAll(".memory, .cpu")];
      await doc.l10n.translateElements(cells);
      Assert.equal(cells[0].textContent, "0B", "A measured zero reads as 0B");
      Assert.equal(
        cells[1].textContent,
        "idle",
        "A measured zero reads as idle"
      );
    }
  } finally {
    BrowserTestUtils.removeTab(tabAboutProcesses);
    for (let tab of tabs) {
      BrowserTestUtils.removeTab(tab);
    }
  }
});
