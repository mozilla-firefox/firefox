/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function testSoloSiteGainingSecondTabFormsExpandedGroup() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org/first",
    "Second same-site tab 1"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  Assert.ok(
    !findSiteHeaderRow(doc, "example.org"),
    "Starts as a solo row with no header"
  );
  let globalToggle = doc.getElementById("expand-all-button");
  Assert.ok(
    globalToggle.disabled,
    "Nothing to expand with only solo tabs, so the toggle is disabled"
  );

  let tbody = doc.getElementById("process-tbody");
  let rowOne = findTabRow(doc, "Second same-site tab 1");
  let rowOneIndex = [...tbody.children].indexOf(rowOne);
  // Freezing after the load, so a slow load can't outlast the freeze.
  let poll = pauseTabViewPolling(doc);
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.org/second",
    "Second same-site tab 2"
  );
  startMouseFreeze(doc);
  await poll();

  Assert.ok(
    !findSiteHeaderRow(doc, "example.org"),
    "The new group waits for the reorder"
  );
  Assert.equal(
    [...tbody.children].indexOf(rowOne),
    rowOneIndex,
    "The existing row stays in place during the mouse freeze"
  );

  info("Sorting reorders despite the freeze");
  clickRow(doc.getElementById("column-memory-resident"));
  let header = findSiteHeaderRow(doc, "example.org");
  Assert.ok(
    header,
    "A site header is created when a second same-site tab opens"
  );
  Assert.equal(
    header.caret.getAttribute("aria-expanded"),
    "true",
    "A solo site that just gained a second tab auto-expands, instead of hiding the tab already being looked at"
  );
  Assert.ok(!globalToggle.disabled, "A multi-tab site makes the toggle active");
  let groupedTabs = childRowsOf(header).map(row => row.tabData.tab);
  Assert.ok(
    groupedTabs.length == 2 &&
      groupedTabs.includes(tabOne) &&
      groupedTabs.includes(tabTwo),
    "Both same-site tabs are placed under their site header"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testFileTabsGroupUnderScheme() {
  let fileURL = Services.io.newFileURI(
    Services.dirsvc.get("TmpD", Ci.nsIFile)
  ).spec;
  let fileTabs = [
    await BrowserTestUtils.openNewForegroundTab(gBrowser, fileURL),
    await BrowserTestUtils.openNewForegroundTab(gBrowser, fileURL),
  ];

  let { tabAboutProcesses, doc } = await openTabView();
  Assert.ok(
    findSiteHeaderRow(doc, "file"),
    "file: tabs group under their scheme, not a blank site"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  for (let tab of fileTabs) {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function testSiteGroupingRendersHeaderAndChildRows() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Group render 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Group render 2"
  );
  let soloTab = await setupTabWithOriginAndTitle(
    "https://example.net",
    "Group render solo"
  );

  let { tabAboutProcesses, doc } = await openTabView();

  let headerRow = findSiteHeaderRow(doc, "example.org");
  Assert.ok(headerRow, "A site-header row exists for the grouped site");
  let countBadge = headerRow.querySelector(".site-tab-count");
  await doc.l10n.translateElements([countBadge]);
  Assert.equal(
    countBadge.textContent,
    "(2 tabs)",
    "Tab count badge reflects the two grouped tabs"
  );
  Assert.equal(
    childRowsOf(headerRow).length,
    2,
    "Two child rows follow the header"
  );

  Assert.ok(
    !findSiteHeaderRow(doc, "example.net"),
    "A single-tab site gets no header row"
  );
  let soloRow = findTabRow(doc, "Group render solo");
  Assert.ok(
    soloRow && !soloRow.classList.contains("child-row"),
    "The solo tab renders as a plain, non-child row"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
  BrowserTestUtils.removeTab(soloTab);
});

add_task(
  async function testNewSiteGroupStartsCollapsedThenCaretTogglesExpansion() {
    let tabOne = await setupTabWithOriginAndTitle(
      "https://example.org",
      "Collapse toggle 1"
    );
    let tabTwo = await setupTabWithOriginAndTitle(
      "https://example.org",
      "Collapse toggle 2"
    );

    let { tabAboutProcesses, doc } = await openTabView();
    let headerRow = findSiteHeaderRow(doc, "example.org");

    Assert.equal(
      headerRow.caret.getAttribute("aria-expanded"),
      "false",
      "A brand-new multi-tab group starts collapsed"
    );
    for (let child of childRowsOf(headerRow)) {
      Assert.ok(
        child.classList.contains("collapsed"),
        "Child rows are hidden while the group is collapsed"
      );
    }

    info("Clicking the caret expands the group");
    clickRow(headerRow.querySelector(".caret-icon"));
    Assert.equal(headerRow.caret.getAttribute("aria-expanded"), "true");
    for (let child of childRowsOf(headerRow)) {
      Assert.ok(
        !child.classList.contains("collapsed"),
        "Children are visible once expanded"
      );
    }

    info("Clicking the caret again collapses it back");
    clickRow(headerRow.querySelector(".caret-icon"));
    Assert.equal(headerRow.caret.getAttribute("aria-expanded"), "false");

    let label = headerRow.querySelector(".site-label");
    Assert.equal(
      headerRow.caret.getAttribute("aria-labelledby"),
      label.id,
      "The caret is named by its site"
    );
    // Mouse-only, like .site-tab-count -- the caret is the group's one tab stop.
    Assert.ok(!label.hasAttribute("role"));
    Assert.ok(!label.hasAttribute("tabindex"));
    AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
    try {
      clickRow(label);
      Assert.equal(
        headerRow.caret.getAttribute("aria-expanded"),
        "true",
        "Clicking the site label expands the group"
      );
      clickRow(headerRow.querySelector(".site-tab-count"));
      Assert.equal(
        headerRow.caret.getAttribute("aria-expanded"),
        "false",
        "The (N tabs) note toggles it too"
      );
    } finally {
      AccessibilityUtils.resetEnv();
    }

    BrowserTestUtils.removeTab(tabAboutProcesses);
    BrowserTestUtils.removeTab(tabOne);
    BrowserTestUtils.removeTab(tabTwo);
  }
);

add_task(async function testStaleSoloStateDoesNotSurviveSiteDisappearing() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Stale solo 1"
  );

  let { doc, tabAboutProcesses } = await openTabView();
  // So no poll lands between the two new tabs, catching the site as solo.
  let poll = pauseTabViewPolling(doc);
  Assert.ok(
    !findSiteHeaderRow(doc, "example.org"),
    "Starts as a solo row with no header"
  );

  info("Closing the only example.org tab makes the site disappear entirely");
  BrowserTestUtils.removeTab(tabOne);
  await poll();

  info(
    "Two fresh example.org tabs appear together, with no solo phase in between"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Stale solo 2"
  );
  let tabThree = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Stale solo 3"
  );
  await poll();

  let headerRow = findSiteHeaderRow(doc, "example.org");
  Assert.ok(headerRow, "A header row exists for the new group");
  Assert.equal(
    headerRow.caret.getAttribute("aria-expanded"),
    "false",
    "The site's earlier, now-closed solo tab doesn't auto-expand the new group"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabTwo);
  BrowserTestUtils.removeTab(tabThree);
});

add_task(async function testSiteHeaderCheckboxSelectsAllChildTabs() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Header checkbox 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Header checkbox 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  let headerRow = findSiteHeaderRow(doc, "example.org");
  clickRow(headerRow.querySelector(".caret-icon"));
  let children = childRowsOf(headerRow);
  Assert.equal(
    headerRow.checkbox.getAttribute("aria-labelledby"),
    headerRow.label.id,
    "The header checkbox is named by its site"
  );

  info("Checking the header checkbox selects every child tab");
  clickRow(headerRow.querySelector(".select-checkbox"));
  for (let child of children) {
    Assert.ok(child.hasAttribute("selected"), "Child row is selected");
  }
  Assert.ok(headerRow.checkbox.checked, "Header checkbox reads checked");
  Assert.ok(
    !headerRow.checkbox.indeterminate,
    "Not indeterminate when all children are selected"
  );

  info("Deselecting one child makes the header checkbox indeterminate");
  clickRow(children[0].querySelector(".select-checkbox"));
  Assert.ok(!headerRow.checkbox.checked, "No longer fully checked");
  Assert.ok(
    headerRow.checkbox.indeterminate,
    "Indeterminate with a partial child selection"
  );
  Assert.ok(
    headerRow.hasAttribute("selected"),
    "Header highlights while any child is selected"
  );

  info("Checking the header checkbox again re-selects every child");
  clickRow(headerRow.querySelector(".select-checkbox"));
  for (let child of children) {
    Assert.ok(child.hasAttribute("selected"), "Every child selected again");
  }

  info("Unchecking the header checkbox clears every child's selection");
  clickRow(headerRow.querySelector(".select-checkbox"));
  for (let child of children) {
    Assert.ok(!child.hasAttribute("selected"), "Every child cleared");
  }
  Assert.ok(
    !headerRow.hasAttribute("selected"),
    "Header not highlighted with nothing selected"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testSiteRowTogglesToReloadWhenAllTabsDiscarded() {
  let tabOne = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Site reload toggle 1"
  );
  let tabTwo = await setupTabWithOriginAndTitle(
    "https://example.org",
    "Site reload toggle 2"
  );

  let { tabAboutProcesses, doc } = await openTabView();
  // Only the actions' own refreshes can update the rows.
  pauseTabViewPolling(doc);
  let tbody = doc.getElementById("process-tbody");
  let headerRow = findSiteHeaderRow(doc, "example.org");

  info("Making one of the group's own tabs the active tab");
  gBrowser.selectedTab = tabOne;
  Services.fog.testResetFOG();
  info("Double-clicking unload acts once");
  let unloadIcon = headerRow.querySelector(".unload-icon");
  Assert.equal(
    doc.l10n.getAttributes(unloadIcon).args.site,
    "example.org",
    "The site's unload button names its site"
  );
  unloadIcon.click();
  unloadIcon.click();
  await BrowserTestUtils.waitForMutationCondition(
    tbody,
    { subtree: true, childList: true, attributes: true },
    () => findSiteHeaderRow(doc, "example.org")?.querySelector(".reload-icon"),
    {
      msg: "Every tab in the group, the active one included, is unloaded and the site row flips to reload",
    }
  );
  Assert.ok(tabOne.hasAttribute("discarded"));
  Assert.ok(tabTwo.hasAttribute("discarded"));
  assertOneEvent(
    Glean.resourceMonitor.tabActionTaken,
    { action_source: "site_header" },
    "Only one unload is recorded, as a site_header action"
  );
  gBrowser.selectedTab = tabAboutProcesses;

  headerRow = findSiteHeaderRow(doc, "example.org");
  Assert.ok(
    headerRow.classList.contains("killed"),
    "The site row dims once every child tab is unloaded"
  );
  Assert.equal(
    headerRow.querySelector(".memory").textContent,
    "—",
    "A fully-discarded group's memory reads as no-data, not a literal 0B sum"
  );
  await promiseRowActionDone(headerRow);

  headerRow.querySelector(".reload-icon").click();
  await BrowserTestUtils.waitForMutationCondition(
    tbody,
    { subtree: true, childList: true, attributes: true },
    () => findSiteHeaderRow(doc, "example.org")?.querySelector(".unload-icon"),
    { msg: "Reloading flips the site row back to unload right away" }
  );
  for (let tab of [tabOne, tabTwo]) {
    await BrowserTestUtils.waitForMutationCondition(
      tab,
      { attributeFilter: ["discarded"] },
      () => !tab.hasAttribute("discarded"),
      { msg: "Each child tab finishes restoring" }
    );
  }

  BrowserTestUtils.removeTab(tabAboutProcesses);
  BrowserTestUtils.removeTab(tabOne);
  BrowserTestUtils.removeTab(tabTwo);
});

add_task(async function testGlobalExpandCollapseAllSitesSyncsWithRowState() {
  let siteOne = [
    await setupTabWithOriginAndTitle("https://example.org", "Global sync A1"),
    await setupTabWithOriginAndTitle("https://example.org", "Global sync A2"),
  ];
  let siteTwo = [
    await setupTabWithOriginAndTitle("https://example.net", "Global sync B1"),
    await setupTabWithOriginAndTitle("https://example.net", "Global sync B2"),
  ];

  let { tabAboutProcesses, doc } = await openTabView();
  let globalToggle = doc.getElementById("expand-all-button");
  let allExpanded = () => globalToggle.classList.contains("expanded");
  Assert.ok(!allExpanded(), "Starts as expand-all with two collapsed groups");

  info("Clicking the global toggle expands every group");
  clickRow(globalToggle);
  Assert.ok(
    allExpanded(),
    "Flips to collapse-all once every group is expanded"
  );
  for (let siteKey of ["example.org", "example.net"]) {
    Assert.equal(
      findSiteHeaderRow(doc, siteKey).caret.getAttribute("aria-expanded"),
      "true",
      `${siteKey}'s group is expanded`
    );
  }

  info("Collapsing just one group via its own caret desyncs from all-expanded");
  clickRow(findSiteHeaderRow(doc, "example.org").querySelector(".caret-icon"));
  Assert.ok(
    !allExpanded(),
    "The global toggle flips back once not every group is expanded"
  );

  info("Re-expanding that one group via its own caret resyncs to all-expanded");
  clickRow(findSiteHeaderRow(doc, "example.org").querySelector(".caret-icon"));
  Assert.ok(
    allExpanded(),
    "The global toggle flips forward again once every group is expanded"
  );

  info("Clicking the global toggle again collapses every group");
  clickRow(globalToggle);
  Assert.ok(
    !allExpanded(),
    "The global toggle returns to expand-all after collapsing every group"
  );
  for (let siteKey of ["example.org", "example.net"]) {
    Assert.equal(
      findSiteHeaderRow(doc, siteKey).caret.getAttribute("aria-expanded"),
      "false",
      `${siteKey}'s group is collapsed`
    );
  }

  BrowserTestUtils.removeTab(tabAboutProcesses);
  for (let tab of [...siteOne, ...siteTwo]) {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function testNavigationKeepsDisplayedGroupUntilReorder() {
  let siteOne = [
    await setupTabWithOriginAndTitle("https://example.org", "Frozen site A1"),
    await setupTabWithOriginAndTitle("https://example.org", "Frozen site A2"),
  ];
  let siteTwo = [
    await setupTabWithOriginAndTitle("https://example.net", "Frozen site B1"),
    await setupTabWithOriginAndTitle("https://example.net", "Frozen site B2"),
  ];

  let { tabAboutProcesses, doc } = await openTabView();
  let headerOne = findSiteHeaderRow(doc, "example.org");
  let headerTwo = findSiteHeaderRow(doc, "example.net");
  clickRow(headerOne.caret);
  Assert.equal(headerOne.caret.getAttribute("aria-expanded"), "true");
  Assert.equal(headerTwo.caret.getAttribute("aria-expanded"), "false");
  let movingRow = childRowsOf(headerOne).find(
    row => row.tabData.tab == siteOne[0]
  );
  let previousSibling = movingRow.previousElementSibling;
  let poll = pauseTabViewPolling(doc);
  let browser = siteOne[0].linkedBrowser;
  let loaded = BrowserTestUtils.browserLoaded(
    browser,
    false,
    "https://example.net/"
  );
  BrowserTestUtils.startLoadingURIString(browser, "https://example.net/");
  await loaded;
  for (let i = 0; i < 2; i++) {
    startMouseFreeze(doc);
    await poll();
    Assert.ok(
      headerOne.group.tabs.some(tabData => tabData.tab == siteOne[0]),
      "The navigating tab stays in its displayed group during the freeze"
    );
    Assert.ok(
      !headerTwo.group.tabs.some(tabData => tabData.tab == siteOne[0]),
      "The destination group excludes the tab until reorder"
    );
    Assert.equal(movingRow.previousElementSibling, previousSibling);
    Assert.ok(!movingRow.classList.contains("collapsed"));
    Assert.equal(
      movingRow.tabData.siteKey,
      "example.net",
      "Live tab data updates"
    );
  }
  clickRow(doc.getElementById("column-name"));
  Assert.ok(!findSiteHeaderRow(doc, "example.org"));
  Assert.ok(
    headerTwo.group.tabs.some(tabData => tabData.tab == siteOne[0]),
    "The tab joins its destination group when rows reorder"
  );

  BrowserTestUtils.removeTab(tabAboutProcesses);
  for (let tab of [...siteOne, ...siteTwo]) {
    BrowserTestUtils.removeTab(tab);
  }
});

add_task(async function testSiteKeysAndLabels() {
  let extension = ExtensionTestUtils.loadExtension({
    manifest: { name: "Site label test extension" },
  });
  await extension.startup();
  let { tabAboutProcesses } = await openTabView();

  let keys = await SpecialPowers.spawn(
    tabAboutProcesses.linkedBrowser,
    [extension.uuid],
    uuid => {
      function keyFor(spec) {
        return content.getSiteKey(content.Services.io.newURI(spec));
      }
      function labelFor(spec) {
        let uri = content.Services.io.newURI(spec);
        return content.getSiteLabel(uri, content.getSiteKey(uri));
      }
      return {
        ip: keyFor("http://127.0.0.1/"),
        singleLabelHost: keyFor("http://localhost/"),
        aboutBlank: keyFor("about:blank"),
        aboutReader: keyFor("about:reader?url=https://example.com/"),
        data: keyFor("data:text/html,<p>hi</p>"),
        idnKey: keyFor("https://sub1.xn--hxajbheg2az3al.xn--jxalpdlp/"),
        idnLabel: labelFor("https://sub1.xn--hxajbheg2az3al.xn--jxalpdlp/"),
        extensionLabel: labelFor(`moz-extension://${uuid}/page.html`),
      };
    }
  );
  await extension.unload();

  Assert.equal(
    keys.idnKey,
    "xn--hxajbheg2az3al.xn--jxalpdlp",
    "Sites are keyed by their ASCII form"
  );
  Assert.equal(
    keys.idnLabel,
    "παράδειγμα.δοκιμή",
    "...but labeled with punycode decoded"
  );
  Assert.equal(
    keys.extensionLabel,
    "Site label test extension",
    "Extension pages are labeled with the extension's name"
  );

  Assert.equal(keys.ip, "127.0.0.1", "IP host falls back to uri.host");
  Assert.equal(
    keys.singleLabelHost,
    "localhost",
    "Single-label host falls back to uri.host"
  );
  Assert.equal(keys.aboutBlank, "about:blank", "about: pages group per page");
  Assert.equal(keys.aboutReader, "about:reader", "...ignoring the query");
  Assert.equal(keys.data, "data", "Other host-less URIs group by scheme");

  BrowserTestUtils.removeTab(tabAboutProcesses);
});
