/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const SPECULATIONS_ROOT =
  URL_ROOT.replace("http:", "https:") + "resources/speculative-loads/";
const SPECULATIONS_PAGE = SPECULATIONS_ROOT + "index.html";

add_setup(async function () {
  await enableApplicationPanel();
  await pushPref("dom.speculation_rules.enabled", true);
});

function getRows(doc) {
  return Array.from(doc.querySelectorAll(".js-speculations-table tbody tr"));
}

async function initiatePrefetches(browser, urls) {
  await SpecialPowers.spawn(browser, [urls], targets => {
    const rule = content.document.createElement("script");
    rule.type = "speculationrules";
    rule.textContent = JSON.stringify({
      prefetch: [{ source: "list", eagerness: "immediate", urls: targets }],
    });
    content.document.head.appendChild(rule);
  });
}

add_task(async function test_prefetch_lifecycle_and_navigation() {
  const { panel, tab, commands } =
    await openNewTabAndApplicationPanel(SPECULATIONS_PAGE);
  const doc = panel.panelWin.document;
  await selectPage(panel, "speculative-loads");
  ok(doc.querySelector(".js-speculations-empty"), "The empty state is shown");

  const panes = doc.querySelectorAll(".speculative-loads-pane");
  is(
    panel.panelWin.getComputedStyle(panes[0]).borderBottomStyle,
    "solid",
    "The page status retains its separator"
  );
  is(
    panel.panelWin.getComputedStyle(panes[1]).borderBottomStyle,
    "none",
    "The speculations list has no extra bottom border"
  );

  const key = "application-speculative-loads";
  const pendingURL = SPECULATIONS_ROOT + "target.sjs?hold=" + key;
  const failureURL = SPECULATIONS_ROOT + "target.sjs?failure=" + key;
  const releaseURL = SPECULATIONS_ROOT + "target.sjs?release=" + key;
  registerCleanupFunction(async () => {
    await fetch(releaseURL);
  });

  await initiatePrefetches(tab.linkedBrowser, [pendingURL, failureURL]);
  await waitFor(() => getRows(doc).length === 2);
  await waitFor(() =>
    getRows(doc).some(row => row.dataset.status === "failure")
  );

  const rows = getRows(doc);
  const pendingRow = rows.find(row => row.cells[0].textContent === pendingURL);
  const failedRow = rows.find(row => row.cells[0].textContent === failureURL);
  is(
    pendingRow.dataset.status,
    "pending",
    "An incomplete response remains pending"
  );
  is(failedRow.dataset.status, "failure", "An HTTP error is a failure");
  is(pendingRow.cells[1].textContent, "Prefetch", "The action is displayed");
  is(doc.querySelectorAll(".js-speculations-table th").length, 3);

  await SpecialPowers.spawn(tab.linkedBrowser, [releaseURL], async url => {
    await content.fetch(url);
  });
  await waitFor(() => pendingRow.dataset.status === "success");
  is(getRows(doc).length, 2, "A regular fetch is not listed as a speculation");
  ok(!doc.querySelector(".js-speculations-empty"), "The empty state is hidden");

  await navigateTo(SPECULATIONS_PAGE);
  await waitFor(() => doc.querySelector(".js-speculations-empty"));
  is(
    getRows(doc).length,
    0,
    "Reload clears the previous document's speculations"
  );

  const nextURL = SPECULATIONS_ROOT + "target.sjs?success=after-reload";
  await initiatePrefetches(tab.linkedBrowser, [nextURL]);
  await waitFor(() => getRows(doc)[0]?.dataset.status === "success");
  is(
    getRows(doc)[0].cells[0].textContent,
    nextURL,
    "Capture continues after reload"
  );

  await navigateTo("https://example.org/");
  await waitFor(() => doc.querySelector(".js-speculations-empty"));
  is(getRows(doc).length, 0, "A process-changing navigation clears old rows");
  await commands.client.waitForRequestsToSettle();
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_existing_network_resources() {
  const tab = await addTab(SPECULATIONS_PAGE);
  const toolbox = await gDevTools.showToolboxForTab(tab, {
    toolId: "netmonitor",
  });
  const { resourceCommand } = toolbox.commands;
  const url = SPECULATIONS_ROOT + "target.sjs?success=existing";
  let complete = false;
  const onAvailable = () => {};
  const onUpdated = updates => {
    complete ||= updates.some(
      ({ resource }) => resource.url === url && resource.responseEndAvailable
    );
  };
  await resourceCommand.watchResources([resourceCommand.TYPES.NETWORK_EVENT], {
    onAvailable,
    onUpdated,
  });
  await initiatePrefetches(tab.linkedBrowser, [url]);
  await waitFor(() => complete);

  const panel = await toolbox.selectTool("application");
  await selectPage(panel, "speculative-loads");
  const doc = panel.panelWin.document;
  await waitFor(() => getRows(doc)[0]?.dataset.status === "success");
  is(getRows(doc).length, 1, "A previously captured request is replayed once");
  is(getRows(doc)[0].cells[0].textContent, url);

  resourceCommand.unwatchResources([resourceCommand.TYPES.NETWORK_EVENT], {
    onAvailable,
    onUpdated,
  });
  await toolbox.commands.client.waitForRequestsToSettle();
  await BrowserTestUtils.removeTab(tab);
});

add_task(async function test_feature_disabled() {
  await pushPref("dom.speculation_rules.enabled", false);
  const { panel, tab } = await openNewTabAndApplicationPanel(SPECULATIONS_PAGE);
  ok(
    !panel.panelWin.document.querySelector(".js-sidebar-speculative-loads"),
    "The sidebar item is hidden when the feature is disabled"
  );
  ok(
    !panel.panelWin.Application._watchedResources.includes("network-event"),
    "The disabled feature does not add network observation"
  );
  await BrowserTestUtils.removeTab(tab);
  await SpecialPowers.popPrefEnv();
});
